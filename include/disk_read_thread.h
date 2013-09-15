#ifndef __DISK_READ_THREAD_H__
#define __DISK_READ_THREAD_H__

#include <string>
#include <tr1/unordered_map>

#include "aio_private.h"
#include "io_request.h"
#include "container.h"
#include "file_partition.h"
#include "messaging.h"

void *process_requests(void *arg);

class async_io;

class disk_read_thread
{
	static const int LOCAL_BUF_SIZE = 16;

	msg_queue<io_request> queue;
	msg_queue<io_request> low_prio_queue;
	logical_file_partition partition;
	std::vector<file_mapper *> open_files;

	pthread_t id;
	async_io *aio;
	int node_id;
	int num_accesses;
	int num_low_prio_accesses;
	int num_ignored_low_prio_accesses;
#ifdef STATISTICS
	long tot_flush_delay;	// in us
	long max_flush_delay;
	long min_flush_delay;
#endif

	atomic_integer flush_counter;

	int process_low_prio_msg(message<io_request> &low_prio_msg,
			std::tr1::unordered_map<io_interface *, int> &ignored_flushes);

public:
	disk_read_thread(const logical_file_partition &partition, int node_id);

	msg_queue<io_request> *get_queue() {
		return &queue;
	}

	msg_queue<io_request> *get_low_prio_queue() {
		return &low_prio_queue;
	}

	int get_node_id() const {
		return node_id;
	}

	int get_num_accesses() const {
		return num_accesses;
	}

	int get_num_low_prio_accesses() const {
		return num_low_prio_accesses;
	}

	int get_num_ignored_low_prio_accesses() const {
		return num_ignored_low_prio_accesses;
	}

	int get_num_iowait() const {
		return aio->get_num_iowait();
	}

	int get_num_completed_reqs() const {
		return aio->get_num_completed_reqs();
	}

	const std::string get_file_name() const {
		if (open_files.empty())
			return "";

		logical_file_partition *part = partition.create_file_partition(open_files[0]);
		std::string name = part->get_file_name(0);
		delete part;
		return name;
	}

	/**
	 * Flush threads asynchronously.
	 * The invoker of this function shouldn't be the I/O thread.
	 * So we need to wake up the I/O thread and notify the I/O thread
	 * to flush requests.
	 */
	void flush_requests() {
		flush_counter.inc(1);
		// If the I/O thread is blocked by the request queue, we should
		// wake the thread up.
		queue.wakeup();
	}

	// It open a new file. The mapping is still the same.
	int open_file(file_mapper *mapper) {
		open_files.push_back(mapper);
		logical_file_partition *part = partition.create_file_partition(mapper);
		int ret = aio->open_file(*part);
		delete part;
		return ret;
	}

	~disk_read_thread() {
		delete aio;
	}

	void run();
};

#endif
