#ifndef _RELAY_CONNECTION_H
#define _RELAY_CONNECTION_H

#include <string>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <list>
#include <vector>

enum DisconnectFlags {
	DISCONNECT_STARTED = 1,
	DISCONNECT_PRINT_AND_CLOSE = 2,
	DISCONNECT_GLOBAL_THREAD_DONE = 4,
	DISCONNECT_COMPLETE = 8,
};

class Connection {
private:
	const int sock;
	std::mutex send_mutex, send_bytes_mutex;
	int outside_send_mutex_token;

	std::function<void(void)> on_disconnect;

	std::list<std::shared_ptr<std::vector<unsigned char> > > outbound_primary_queue, outbound_secondary_queue;
	size_t primary_writepos, secondary_writepos;

	// During initial_outbound_throttle, total_waiting_size is allowed to exceed the
	// usual outbound buffer size but only by initial_outbound_bytes
	//
	// initial_outbound_bytes is defined as the quantity of bytes sent with send_mutex_token
	// (not mabye_send, do_send), during initial_outbound_throttle
	bool initial_outbound_throttle;
	int64_t initial_outbound_bytes;
	std::atomic<int64_t> total_waiting_size;
	std::chrono::steady_clock::time_point earliest_next_write;

	std::mutex read_mutex;
	std::condition_variable read_cv;
	size_t readpos;
	std::atomic<int64_t> total_inbound_size;
	std::list<std::unique_ptr<std::vector<unsigned char> > > inbound_queue;

	std::thread *user_thread;
	int sock_errno;

	std::atomic<int> disconnectFlags;
public:
	const std::string host;

	Connection(int sockIn, std::string hostIn, std::function<void(void)> on_disconnect_in) :
			sock(sockIn), outside_send_mutex_token(0xdeadbeef * (unsigned long)this), on_disconnect(on_disconnect_in),
			primary_writepos(0), secondary_writepos(0), initial_outbound_throttle(false), initial_outbound_bytes(0),
			total_waiting_size(0), earliest_next_write(std::chrono::steady_clock::time_point::min()),
			readpos(0), total_inbound_size(0), sock_errno(0), disconnectFlags(0), host(hostIn)
		{}

protected:
	void construction_done() {
		user_thread = new std::thread(do_setup_and_read, this);
	}

public:
	virtual ~Connection();

	int getDisconnectFlags() { return disconnectFlags; }

protected:
	virtual void net_process(const std::function<void(const char*)>& disconnect)=0;
	ssize_t read_all(char *buf, size_t nbyte);

	void do_send_bytes(const char *buf, size_t nbyte, int send_mutex_token=0) {
		do_send_bytes(std::make_shared<std::vector<unsigned char> >((unsigned char*)buf, (unsigned char*)buf + nbyte), send_mutex_token);
	}

	void do_send_bytes(const std::shared_ptr<std::vector<unsigned char> >& bytes, int send_mutex_token=0);
	void maybe_send_bytes(const std::shared_ptr<std::vector<unsigned char> >& bytes, int send_mutex_token=0);

	// See the comment above initial_outbound_throttle for special meanings of the send_mutex_tokens
	int get_send_mutex() { send_mutex.lock(); return (outside_send_mutex_token *= 0xdeadbeef); }
	void release_send_mutex(int send_mutex_token) { outside_send_mutex_token *= 0xdeadbeef; send_mutex.unlock(); }
	void do_throttle_outbound() { initial_outbound_throttle = true; }

public:
	void disconnect_from_outside(const char* reason);

private:
	void disconnect(const char* reason);
	static void do_setup_and_read(Connection* me);

	friend class GlobalNetProcess;
};

class OutboundPersistentConnection {
private:
	class OutboundConnection : public Connection {
	private:
		OutboundPersistentConnection *parent;
		void net_process(const std::function<void(const char*)>& disconnect) { parent->net_process(disconnect); }

	public:
		OutboundConnection(int sockIn, OutboundPersistentConnection* parentIn) :
				Connection(sockIn, parentIn->serverHost, [&](void) { parent->reconnect("THIS SHOULD NEVER PRINT"); }),
				parent(parentIn)
			{ }

		ssize_t read_all(char *buf, size_t nbyte) { return Connection::read_all(buf, nbyte); }
		void do_send_bytes(const char *buf, size_t nbyte) { return Connection::do_send_bytes(buf, nbyte); }
		void do_send_bytes(const std::shared_ptr<std::vector<unsigned char> >& bytes) { return Connection::do_send_bytes(bytes); }
		void construction_done() { Connection::construction_done(); }
	};

	const std::string serverHost;
	const uint16_t serverPort;

	std::atomic<unsigned long> connection;
	static_assert(sizeof(unsigned long) == sizeof(OutboundConnection*), "unsigned long must be the size of a pointer");

public:
	OutboundPersistentConnection(std::string serverHostIn, uint16_t serverPortIn) :
			serverHost(serverHostIn), serverPort(serverPortIn), connection(0)
		{}

protected:
	void construction_done() { std::thread(do_connect, this).detach(); }

	virtual void on_disconnect()=0;
	virtual void net_process(const std::function<void(const char*)>& disconnect)=0;
	ssize_t read_all(char *buf, size_t nbyte) { return ((OutboundConnection*)connection.load())->read_all(buf, nbyte); } // Only allowed from within net_process

	void maybe_do_send_bytes(const char *buf, size_t nbyte) {
		OutboundConnection* conn = (OutboundConnection*)connection.load();
		if (conn)
			conn->do_send_bytes(buf, nbyte);
	}
	void maybe_do_send_bytes(const std::shared_ptr<std::vector<unsigned char> >& bytes) {
		OutboundConnection* conn = (OutboundConnection*)connection.load();
		if (conn)
			conn->do_send_bytes(bytes);
	}

private:
	void reconnect(std::string disconnectReason); // Called only after DISCONNECT_COMPLETE in Connection, or before Connection::construction_done()
	static void do_connect(OutboundPersistentConnection* me);
};

#endif
