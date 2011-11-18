#ifndef __ASSOCIATIVE_CACHE_H__
#define __ASSOCIATIVE_CACHE_H__

#include "cache.h"

#define CELL_SIZE 8

#ifdef STATISTICS
static volatile int avail_cells;
static int num_wait_unused;
#endif

class hash_cell
{
	pthread_spinlock_t _lock;
	page_buffer<thread_safe_page> *buf;
	long page_buf;

	/* this function has to be called with lock held */
	thread_safe_page *get_empty_page() {
		thread_safe_page *ret = buf->get_empty_page();
		/*
		 * each time we select a page to evict,
		 * it's possible that it's still used by some
		 * other threads. We need to wait for other threads
		 * to finish using it.
		 */
		// TODO I assume this situation is rare
		while (ret->get_ref()) {
#ifdef STATISTICS
			num_wait_unused++;
#endif
			pthread_spin_unlock(&_lock);
			ret->wait_unused();
			pthread_spin_lock(&_lock);
		}
		ret->set_data_ready(false);
		ret->inc_ref();
		return ret;
	}

public:
	hash_cell() {
		buf = NULL;
		pthread_spin_init(&_lock, PTHREAD_PROCESS_PRIVATE);
	}

	~hash_cell() {
		if (buf)
			delete buf;
		pthread_spin_destroy(&_lock);
	}

	void set_pages(long page_buf) {
		this->page_buf = page_buf;
	}

	/**
	 * search for a page with the offset.
	 * If the page doesn't exist, return an empty page.
	 */
	page *search(off_t off) {
		thread_safe_page *ret = NULL;
		pthread_spin_lock(&_lock);
		/* if no page has been added, return immediately. */
		if (buf == NULL) {
			buf = new page_buffer<thread_safe_page> (CELL_SIZE, page_buf);
#ifdef STATISTICS
			__sync_fetch_and_add(&avail_cells, 1);
#endif
			ret = get_empty_page();
			ret->set_offset(off);
			pthread_spin_unlock(&_lock);
			return ret;
		}

		for (int i = 0; i < CELL_SIZE; i++) {
			if (buf->get_page(i)->get_offset() == off) {
				ret = buf->get_page(i);
				break;
			}
		}
		if (ret == NULL) {
			ret = get_empty_page();
			ret->set_offset(off);
			pthread_spin_unlock(&_lock);
			return ret;
		}
		/* it's possible that the data in the page isn't ready */
		ret->inc_ref();
		pthread_spin_unlock(&_lock);
		return ret;
	}
};

class associative_cache: public page_cache
{
	hash_cell *cells;
	int ncells;
	// TODO maybe it's not a good hash function
	int hash(off_t offset) {
		return offset / PAGE_SIZE % ncells;
	}

public:
	associative_cache(long cache_size) {
		printf("associative cache is used\n");
		int npages = cache_size / PAGE_SIZE;
		ncells = npages / CELL_SIZE;
		cells = new hash_cell[ncells];
		for (int i = 0; i < ncells; i++)
			cells[i].set_pages(i * PAGE_SIZE * CELL_SIZE);
	}

	~associative_cache() {
		delete [] cells;
	}

	page *search(off_t offset) {
		hash_cell *cell = &cells[hash(offset)];
		return cell->search(offset);
	}
};

#endif
