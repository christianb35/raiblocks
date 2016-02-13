#pragma once

#include <rai/secure.hpp>
#include <rai/node/common.hpp>

#include <queue>
#include <unordered_set>

#include <boost/log/sources/logger.hpp>

namespace rai
{
class block_synchronization
{
public:
    block_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    ~block_synchronization ();
    // Return true if target already has block
    virtual bool synchronized (rai::transaction &, rai::block_hash const &) = 0;
    virtual std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) = 0;
    // return true if all dependencies are synchronized
    bool add_dependency (rai::transaction &, rai::block const &);
    bool fill_dependencies (rai::transaction &);
    bool synchronize_one (rai::transaction &);
    bool synchronize (rai::transaction &, rai::block_hash const &);
    std::stack <rai::block_hash> blocks;
    std::unordered_set <rai::block_hash> sent;
	boost::log::sources::logger & log;
    std::function <void (rai::transaction &, rai::block const &)> target;
    rai::block_store & store;
};
class pull_synchronization : public rai::block_synchronization
{
public:
    pull_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::transaction &, rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) override;
};
class push_synchronization : public rai::block_synchronization
{
public:
    push_synchronization (boost::log::sources::logger &, std::function <void (rai::transaction &, rai::block const &)> const &, rai::block_store &);
    bool synchronized (rai::transaction &, rai::block_hash const &) override;
    std::unique_ptr <rai::block> retrieve (rai::transaction &, rai::block_hash const &) override;
};
class node;
class bootstrap_client : public std::enable_shared_from_this <bootstrap_client>
{
public:
	bootstrap_client (std::shared_ptr <rai::node>, std::function <void ()> const & = [] () {});
    ~bootstrap_client ();
    void run (rai::tcp_endpoint const &);
    void connect_action ();
    void sent_request (boost::system::error_code const &, size_t);
    std::shared_ptr <rai::node> node;
    boost::asio::ip::tcp::socket socket;
	std::function <void ()> completion_action;
};
class frontier_req_client : public std::enable_shared_from_this <rai::frontier_req_client>
{
public:
    frontier_req_client (std::shared_ptr <rai::bootstrap_client> const &);
    ~frontier_req_client ();
    void receive_frontier ();
    void received_frontier (boost::system::error_code const &, size_t);
    void request_account (rai::account const &);
	void unsynced (MDB_txn *, rai::account const &, rai::block_hash const &);
    void completed_requests ();
    void completed_pulls ();
    void completed_pushes ();
	void next ();
    std::unordered_map <rai::account, rai::block_hash> pulls;
    std::array <uint8_t, 200> receive_buffer;
    std::shared_ptr <rai::bootstrap_client> connection;
	rai::account current;
	rai::account_info info;
};
class bulk_pull_client : public std::enable_shared_from_this <rai::bulk_pull_client>
{
public:
    bulk_pull_client (std::shared_ptr <rai::frontier_req_client> const &);
    ~bulk_pull_client ();
    void request ();
    void receive_block ();
    void received_type ();
    void received_block (boost::system::error_code const &, size_t);
    void process_end ();
	void block_flush ();
	rai::block_hash first ();
    std::array <uint8_t, 200> receive_buffer;
    std::shared_ptr <rai::frontier_req_client> connection;
	size_t const block_count = 4096;
	std::vector <std::unique_ptr <rai::block>> blocks;
    std::unordered_map <rai::account, rai::block_hash>::iterator current;
    std::unordered_map <rai::account, rai::block_hash>::iterator end;
};
class bulk_push_client : public std::enable_shared_from_this <rai::bulk_push_client>
{
public:
    bulk_push_client (std::shared_ptr <rai::frontier_req_client> const &);
    ~bulk_push_client ();
    void start ();
    void push ();
    void push_block (rai::block const &);
    void send_finished ();
    std::shared_ptr <rai::frontier_req_client> connection;
    rai::push_synchronization synchronization;
};

class bootstrap_initiator
{
public:
	bootstrap_initiator (rai::node &);
	void warmup (rai::endpoint const &);
	void bootstrap (rai::endpoint const &);
    void bootstrap_any ();
	void initiate (rai::endpoint const &);
	void notify_listeners ();
	std::vector <std::function <void (bool)>> observers;
	std::mutex mutex;
	rai::node & node;
	bool in_progress;
	std::unordered_set <rai::endpoint> warmed_up;
};
class bootstrap_listener
{
public:
    bootstrap_listener (boost::asio::io_service &, uint16_t, rai::node &);
    void start ();
    void stop ();
    void accept_connection ();
    void accept_action (boost::system::error_code const &, std::shared_ptr <boost::asio::ip::tcp::socket>);
    rai::tcp_endpoint endpoint ();
    boost::asio::ip::tcp::acceptor acceptor;
    rai::tcp_endpoint local;
    boost::asio::io_service & service;
    rai::node & node;
    bool on;
};
class message;
class bootstrap_server : public std::enable_shared_from_this <rai::bootstrap_server>
{
public:
    bootstrap_server (std::shared_ptr <boost::asio::ip::tcp::socket>, std::shared_ptr <rai::node>);
    ~bootstrap_server ();
    void receive ();
    void receive_header_action (boost::system::error_code const &, size_t);
    void receive_bulk_pull_action (boost::system::error_code const &, size_t);
    void receive_frontier_req_action (boost::system::error_code const &, size_t);
    void receive_bulk_push_action ();
    void add_request (std::unique_ptr <rai::message>);
    void finish_request ();
    void run_next ();
    std::array <uint8_t, 128> receive_buffer;
    std::shared_ptr <boost::asio::ip::tcp::socket> socket;
    std::shared_ptr <rai::node> node;
    std::mutex mutex;
    std::queue <std::unique_ptr <rai::message>> requests;
};
class bulk_pull;
class bulk_pull_server : public std::enable_shared_from_this <rai::bulk_pull_server>
{
public:
    bulk_pull_server (std::shared_ptr <rai::bootstrap_server> const &, std::unique_ptr <rai::bulk_pull>);
    void set_current_end ();
    std::unique_ptr <rai::block> get_next ();
    void send_next ();
    void sent_action (boost::system::error_code const &, size_t);
    void send_finished ();
    void no_block_sent (boost::system::error_code const &, size_t);
    std::shared_ptr <rai::bootstrap_server> connection;
    std::unique_ptr <rai::bulk_pull> request;
    std::vector <uint8_t> send_buffer;
    rai::block_hash current;
};
class bulk_push_server : public std::enable_shared_from_this <rai::bulk_push_server>
{
public:
    bulk_push_server (std::shared_ptr <rai::bootstrap_server> const &);
    void receive ();
    void receive_block ();
    void received_type ();
    void received_block (boost::system::error_code const &, size_t);
    void process_end ();
    std::array <uint8_t, 256> receive_buffer;
    std::shared_ptr <rai::bootstrap_server> connection;
};
class frontier_req;
class frontier_req_server : public std::enable_shared_from_this <rai::frontier_req_server>
{
public:
    frontier_req_server (std::shared_ptr <rai::bootstrap_server> const &, std::unique_ptr <rai::frontier_req>);
    void skip_old ();
    void send_next ();
    void sent_action (boost::system::error_code const &, size_t);
    void send_finished ();
    void no_block_sent (boost::system::error_code const &, size_t);
	void next ();
    std::shared_ptr <rai::bootstrap_server> connection;
	rai::account current;
	rai::account_info info;
    std::unique_ptr <rai::frontier_req> request;
    std::vector <uint8_t> send_buffer;
    size_t count;
};
}