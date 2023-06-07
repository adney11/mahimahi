/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <limits>
#include <cassert>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <cmath>

#include "link_queue.hh"
#include "timestamp.hh"
#include "util.hh"
#include "ezio.hh"
#include "abstract_packet_queue.hh"

#include <errno.h>

using namespace std;

LinkQueue::LinkQueue( const string & link_name, const string & filename, const string & logfile,
                      const bool repeat, const bool graph_throughput, const bool graph_delay,
                      unique_ptr<AbstractPacketQueue> && packet_queue,
                      const string & command_line )
    : link_name_( link_name ),
      next_delivery_( 0 ),
      schedule_(),
      base_timestamp_( timestamp() ),
      packet_queue_( move( packet_queue ) ),
      packet_in_transit_( "", 0 ),
      packet_in_transit_bytes_left_( 0 ),
      output_queue_(),
      log_(),
      throughput_graph_( nullptr ),
      delay_graph_( nullptr ),
      repeat_( repeat ),
      finished_( false ),
#ifdef MDEBUG
      debug_(),
#endif
      last_recieved_bw_(),
      live_fd_()
{
    assert_not_root();

    /* open filename and load schedule */
    ifstream trace_file( filename );

    if ( not trace_file.good() ) {
        throw runtime_error( filename + ": error opening for reading" );
    }

    string line;

    while ( trace_file.good() and getline( trace_file, line ) ) {
        if ( line.empty() ) {
            throw runtime_error( filename + ": invalid empty line" );
        }

        const uint64_t ms = myatoi( line );

        if ( not schedule_.empty() ) {
            if ( ms < schedule_.back() ) {
                throw runtime_error( filename + ": timestamps must be monotonically nondecreasing" );
            }
        }

        schedule_.emplace_back( ms );
    }

    if ( schedule_.empty() ) {
        throw runtime_error( filename + ": no valid timestamps found" );
    }

    if ( schedule_.back() == 0 ) {
        throw runtime_error( filename + ": trace must last for a nonzero amount of time" );
    }

    /* open logfile if called for */
    if ( not logfile.empty() ) {
        log_.reset( new ofstream( logfile ) );
        if ( not log_->good() ) {
            throw runtime_error( logfile + ": error opening for writing" );
        }

        *log_ << "# mahimahi mm-link (" << link_name << ") [" << filename << "] > " << logfile << endl;
        *log_ << "# command line: " << command_line << endl;
        *log_ << "# queue: " << packet_queue_->to_string() << endl;
        *log_ << "# init timestamp: " << initial_timestamp() << endl;
        *log_ << "# base timestamp: " << base_timestamp_ << endl;
        const char * prefix = getenv( "MAHIMAHI_SHELL_PREFIX" );
        if ( prefix ) {
            *log_ << "# mahimahi config: " << prefix << endl;
        }
    }

    /* create graphs if called for */
    if ( graph_throughput ) {
        throughput_graph_.reset( new BinnedLiveGraph( link_name + " [" + filename + "]",
                                                      { make_tuple( 1.0, 0.0, 0.0, 0.25, true ),
                                                        make_tuple( 0.0, 0.0, 0.4, 1.0, false ),
                                                        make_tuple( 1.0, 0.0, 0.0, 0.5, false ) },
                                                      "throughput (Mbps)",
                                                      8.0 / 1000000.0,
                                                      true,
                                                      500,
                                                      [] ( int, int & x ) { x = 0; } ) );
    }

    if ( graph_delay ) {
        delay_graph_.reset( new BinnedLiveGraph( link_name + " delay [" + filename + "]",
                                                 { make_tuple( 0.0, 0.25, 0.0, 1.0, false ) },
                                                 "queueing delay (ms)",
                                                 1, false, 250,
                                                 [] ( int, int & x ) { x = -1; } ) );
    }


    /* Updates DONE
        1. Open a file descriptor for the adversarial pipe using open() - DONE
    */

    if (link_name_ == "Downlink") {
#ifdef MDEBUG
        debug_.reset( new ofstream( "/newhome/link_debug" ) );
        if ( not debug_->good() ) {
            throw runtime_error( "/newhome/link_debug couldn't be opened for writing ");
        }
        DLOG( *debug_ << "INIT: Made Debug file successfully" << endl; );
#endif

        live_fd_ = open("/newhome/adversary_update", O_RDONLY | O_NONBLOCK);
        if ( live_fd_ < 0 ) {
            DLOG( *debug_ << "got error: " << strerror(errno) << " with errno: " << errno << endl; );
            throw runtime_error( "was unable to open /newhome/adversary_update" );
        } 
        else {
            DLOG( *debug_ << "live_fd_ opened with value: " << live_fd_ << endl; );
        }
        DLOG( *debug_ << "Downlink here - opened pipe for reading" << endl; );
    }
    last_recieved_bw_ = 0;
}

void LinkQueue::record_arrival( const uint64_t arrival_time, const size_t pkt_size )
{
    /* log it */
    if ( log_ ) {
        *log_ << arrival_time << " + " << pkt_size << endl;
    }

    /* meter it */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 1, pkt_size );
    }
}

void LinkQueue::record_drop( const uint64_t time, const size_t pkts_dropped, const size_t bytes_dropped)
{
    /* log it */
    if ( log_ ) {
        *log_ << time << " d " << pkts_dropped << " " << bytes_dropped << endl;
    }
}

void LinkQueue::record_departure_opportunity( void )
{
    /* log the delivery opportunity */
    if ( log_ ) {
        *log_ << next_delivery_time() << " # " << PACKET_SIZE << endl;
    }

    /* meter the delivery opportunity */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 0, PACKET_SIZE );
    }    
}

void LinkQueue::record_departure( const uint64_t departure_time, const QueuedPacket & packet )
{
    /* log the delivery */
    if ( log_ ) {
        *log_ << departure_time << " - " << packet.contents.size()
              << " " << departure_time - packet.arrival_time << endl;
    }

    /* meter the delivery */
    if ( throughput_graph_ ) {
        throughput_graph_->add_value_now( 2, packet.contents.size() );
    }

    if ( delay_graph_ ) {
        delay_graph_->set_max_value_now( 0, departure_time - packet.arrival_time );
    }    
}

void LinkQueue::read_packet( const string & contents )
{
    const uint64_t now = timestamp();

    if ( contents.size() > PACKET_SIZE ) {
        throw runtime_error( "packet size is greater than maximum" );
    }

    rationalize( now );

    record_arrival( now, contents.size() );

    unsigned int bytes_before = packet_queue_->size_bytes();
    unsigned int packets_before = packet_queue_->size_packets();

    packet_queue_->enqueue( QueuedPacket( contents, now ) );

    assert( packet_queue_->size_packets() <= packets_before + 1 );
    assert( packet_queue_->size_bytes() <= bytes_before + contents.size() );
    
    unsigned int missing_packets = packets_before + 1 - packet_queue_->size_packets();
    unsigned int missing_bytes = bytes_before + contents.size() - packet_queue_->size_bytes();
    if ( missing_packets > 0 || missing_bytes > 0 ) {
        record_drop( now, missing_packets, missing_bytes );
    }
}

uint64_t LinkQueue::next_delivery_time( void ) const
{
    if ( finished_ ) {
        return -1;
    } else {
        return schedule_.at( next_delivery_ ) + base_timestamp_;
    }
}

void LinkQueue::use_a_delivery_opportunity( void )
{
    record_departure_opportunity();

    next_delivery_ = (next_delivery_ + 1) % schedule_.size();

    /* wraparound */
    if ( next_delivery_ == 0 ) {
        if ( repeat_ ) {
            base_timestamp_ += schedule_.back();
        } else {
            finished_ = true;
        }
    }
}




std::vector<uint64_t> convertMbpsToPacketOpportunity(double mbps, int reps = 5) {
    const int DENOMINATOR_FOR_1_MBPS = 12;

    double num = mbps;
    double den = DENOMINATOR_FOR_1_MBPS;

    auto isInt = [&](double x) {
        return (std::fmod(x, 1) == 0);
    };

    auto getGCD = [](int x, int y) {
        while ( y > 0) {
            int r = x % y;
            x = y;
            y = r;
        }
        return x;
    };

    while (!isInt(num) || !isInt(den)) {
        num *= 10;
        den *= 10;
    }

    int inum = (int)num;
    int iden = (int)den;
    int gcd;
    while ((gcd = getGCD(inum, iden)) != 1) {
        inum /= gcd;
        iden /= gcd;
    }

    std::vector<uint64_t> packetOpportunity;
    for (int ms = 0; ms < iden * reps; ms = ms + iden) {
        for (int i = 0; i < inum; i++) {
           packetOpportunity.push_back(ms);
        }
    }

    return packetOpportunity;
}





/* emulate the link up to the given timestamp */
/* this function should be called before enqueueing any packets and before
   calculating the wait_time until the next event */
void LinkQueue::rationalize( const uint64_t now )
{

    /* Updates DONE
    1. Add logic to read pipe for bandwidth value here - DONE
    2. Convert bandwidth value into packet_trace vector - DONE
    3. Replace schedule_ vector and reset next_delivery_ to 0 - DONE
    */

    // 1
    if (link_name_ == "Downlink") {
        char buf[BUFSIZ];
        double recvd_bw;
        char* ptr;
        memset(buf, '\0', sizeof(buf));
        int ret = read(live_fd_, &buf, sizeof(buf));
        if ( ret < 0 ) {
            //DLOG( *debug_ << "ret returned " << ret << "with errno: " << strerror(errno) << endl; );
            // do nothing
        } else if ( ret == 0 ) {
            //DLOG( *debug_ << "ret return 0, cause pipe was closed" << endl; );
        } else {
            buf[ret] = '\0';
            DLOG ( *debug_ << "read " << ret << " bytes from pipe" << endl; );
            DLOG( *debug_ << "buf is: " << buf << endl; );
            ptr = strtok (buf, ",");
            recvd_bw = std::stod(ptr);
            if (last_recieved_bw_ != recvd_bw) {
                DLOG( *debug_ << "received new bw: " << recvd_bw << endl; );
                last_recieved_bw_ = recvd_bw;
                // 2
                auto new_schedule = convertMbpsToPacketOpportunity(recvd_bw);
                // schedule_: [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3], 4 pkts 1 ms, 7.5mbps
                // delta recvd_bw: +1.5mbps => 9mbps
                // [0, 0, 0, 4, 4, 4, 8, 8, 8, 12, 12, 12]
                // Q1: we can append  ...2, 3, 3, 3, 3] ->  ...2, 3, 3, 3, 3, 7, 7, 7, 11, 11, ..]
                // Q2: can we know where the current idx of schedule_?
                // 
                // new_schedule: [+1] # length of a 100
                // schedule_: [1, max(2+1, next), 4]
                // 3
                base_timestamp_ += schedule_.at(next_delivery_);
                schedule_.swap(new_schedule);
                next_delivery_ = 0;

            }
        }
    }

    


    while ( next_delivery_time() <= now ) {
        const uint64_t this_delivery_time = next_delivery_time();

        /* burn a delivery opportunity */
        unsigned int bytes_left_in_this_delivery = PACKET_SIZE;
        use_a_delivery_opportunity();

        while ( bytes_left_in_this_delivery > 0 ) {
            if ( not packet_in_transit_bytes_left_ ) {
                if ( packet_queue_->empty() ) {
                    break;
                }
                packet_in_transit_ = packet_queue_->dequeue();
                packet_in_transit_bytes_left_ = packet_in_transit_.contents.size();
            }

            assert( packet_in_transit_.arrival_time <= this_delivery_time );
            assert( packet_in_transit_bytes_left_ <= PACKET_SIZE );
            assert( packet_in_transit_bytes_left_ > 0 );
            assert( packet_in_transit_bytes_left_ <= packet_in_transit_.contents.size() );

            /* how many bytes of the delivery opportunity can we use? */
            const unsigned int amount_to_send = min( bytes_left_in_this_delivery,
                                                     packet_in_transit_bytes_left_ );

            /* send that many bytes */
            packet_in_transit_bytes_left_ -= amount_to_send;
            bytes_left_in_this_delivery -= amount_to_send;

            /* has the packet been fully sent? */
            if ( packet_in_transit_bytes_left_ == 0 ) {
                record_departure( this_delivery_time, packet_in_transit_ );

                /* this packet is ready to go */
                output_queue_.push( move( packet_in_transit_.contents ) );
            }
        }
    }
}

void LinkQueue::write_packets( FileDescriptor & fd )
{
    while ( not output_queue_.empty() ) {
        fd.write( output_queue_.front() );
        output_queue_.pop();
    }
}

unsigned int LinkQueue::wait_time( void )
{
    const auto now = timestamp();

    rationalize( now );

    if ( next_delivery_time() <= now ) {
        return 0;
    } else {
        return next_delivery_time() - now;
    }
}

bool LinkQueue::pending_output( void ) const
{
    return not output_queue_.empty();
}
