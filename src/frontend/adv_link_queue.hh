/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef ADV_LINK_QUEUE_HH
#define ADV_LINK_QUEUE_HH

#include <queue>
#include <cstdint>
#include <string>
#include <fstream>
#include <memory>
#include <thread>

#include "file_descriptor.hh"
#include "binned_livegraph.hh"
#include "abstract_packet_queue.hh"
//#include "adversary.hh"



#define PROBE_PER 100

class Adversary
{
private:
    // define metric counters
    uint64_t num_packet_departure_opportunity_;
    uint64_t num_packet_departure_;

    uint64_t current_queuing_delay_;

    std::unique_ptr<std::ofstream> log_;

    std::atomic<bool> halt_;

    std::exception_ptr probe_thread_exception_;
    std::thread probe_thread_;
    
    uint64_t probe_time_;

    void probe_loop( void );

public:

    Adversary(const std::string & adversary_logfile );
    ~Adversary();
    // define metric calculating functions
    void record_packet_departure_opportunity( void );
    void record_packet_departure( void );
    double get_link_utilization( void );

    void record_queuing_delay( uint64_t queing_delay );
    uint64_t get_queueing_delay( void );

    void log_state( const uint64_t now, double link_util, uint64_t queueing_delay );

    void start_adversary_loop( void );
};


class AdvLinkQueue
{
private:
    const static unsigned int PACKET_SIZE = 1504; /* default max TUN payload size */

    unsigned int next_delivery_;
    std::vector<uint64_t> schedule_;
    uint64_t base_timestamp_;

    std::unique_ptr<AbstractPacketQueue> packet_queue_;
    QueuedPacket packet_in_transit_;
    unsigned int packet_in_transit_bytes_left_;
    std::queue<std::string> output_queue_;

    std::unique_ptr<std::ofstream> log_;
    std::unique_ptr<BinnedLiveGraph> throughput_graph_;
    std::unique_ptr<BinnedLiveGraph> delay_graph_;


    // Adversary
    std::unique_ptr<Adversary> adversary_;
    //std::string adversary_;

    bool repeat_;
    bool finished_;

    uint64_t next_delivery_time( void ) const;

    void use_a_delivery_opportunity( void );

    void record_arrival( const uint64_t arrival_time, const size_t pkt_size );
    void record_drop( const uint64_t time, const size_t pkts_dropped, const size_t bytes_dropped );
    void record_departure_opportunity( void );
    void record_departure( const uint64_t departure_time, const QueuedPacket & packet );

    void rationalize( const uint64_t now );
    void dequeue_packet( void );

public:
    AdvLinkQueue( const std::string & link_name, const std::string & filename, const std::string & logfile,
               const bool repeat, const bool graph_throughput, const bool graph_delay,
               std::unique_ptr<AbstractPacketQueue> && packet_queue,
               const std::string & command_line, const std::string & adversary_logfile);

    void read_packet( const std::string & contents );

    void write_packets( FileDescriptor & fd );

    unsigned int wait_time( void );

    bool pending_output( void ) const;

    bool finished( void ) const { return finished_; }

    void update_schedule( std::vector<uint64_t> new_schedule );
};

#endif /* ADV_LINK_QUEUE_HH */
