/* The MIT License

   Copyright (C) 2012 Zilong Tan (eric.zltan@gmail.com)

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation
   files (the "Software"), to deal in the Software without
   restriction, including without limitation the rights to use, copy,
   modify, merge, publish, distribute, sublicense, and/or sell copies
   of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

#ifndef __ULIB_MAPREDUCE_H
#define __ULIB_MAPREDUCE_H

#include <utility>
#include <ulib/thread.h>
#include <ulib/rand_tpl.h>
#include "store.h"
#include "dataset.h"

namespace ulib {

namespace mapreduce {

// enforce associative property by providing the += operation:
//     associative(V) += V;
// uses can implement different interpretation of += by overloading
// the += operator of either V or associative class.
template<typename V>
class associative {
public:
	typedef V value_type;

	associative(V &val)
	: _value(val)
	{ }

	virtual	associative &
	operator+=(const V &other)
	{
		_value += other;
		return *this;
	}

	operator V()
	{ return _value; }

	const V &
	value() const
	{ return _value; }

protected:
	V &_value;
};

// the abstraction of a mapper, which is qualified by the conversion
// from a record to an intermediate key or value. The R, K and V are
// the record type, key type and value type respectively.
template<typename R, typename K, typename V>
class mapper {
public:
	typedef R record_type;
	typedef K key_type;
	typedef V value_type;

	mapper(const R &rec)
	: _rec(rec)
	{ }

	// compute the intermediate key of a record, the
	// implementation is left to the derived mapper.
	virtual const K
	key() const = 0;

	// compute the value for a record, again the implementation os
	// left to the derived mapper.
	virtual const V
	value() const = 0;

	const R &
	record() const
	{ return _rec; }

protected:
	const R &_rec;
};

// a simple reducer that is no more than associative
template<class V>
class reducer : public associative<V>
{
public:
	typedef typename associative<V>::value_type value_type;

	reducer(V &val)
	: associative<V>(val)
	{ }
};

template<typename K>
class partitioner {
public:
	partitioner(const K &key)
	: _key(key)
	{ }

	// an enhancement on the key hash function, which is implied
	// through the conversion from a key to an integer.
	virtual
	operator size_t() const
	{
		uint64_t h = _key;
		RAND_INT3_MIX64(h);  // a lightweight transformation
		return h;
	}

	// the equality of partitioners is defined as the equality of
	// their keys.
	bool
	operator==(const partitioner &other) const
	{ return _key == other.key(); }

	const K &
	key() const
	{ return _key; }

private:
	K _key;
};

// astraction for a task, in fact it does not follow the rigorous
// MapReduce paradigm of which a task has independent space for both
// input and output. By contrast, in this task abstraction shared the
// same output space.
template<typename S, typename M, typename R, typename I>
class task : public ulib::thread {
public:
	task(S &s, I begin, I end)
	: _store(s), _begin(begin), _end(end)
	{ }

	~task()
	{ stop_and_join(); }

private:
	int
	run()  // should not be called manully, making it private
	{
		for (I i = _begin; i != _end; ++i) {
			M m(*i);
			_store.lock(m.key());
			R(_store[m.key()]) += m.value();
			_store.unlock(m.key());
		}
		return 0;
	}

	S &_store;
	I _begin;
	I _end;
};

// a job is defined as the set of a dataset, a mapper that converts
// data records to key/value pairs, a partitioner that spreads the
// key/value pairs evenly to slots, and a reducer that combine the
// values associated with the same key.
template<
	template<typename K, typename V> class S,  // key-value storage template
	class M,
	class R,
	template<typename K> class P,
	class D>
class job {
public:
	typedef M mapper_type;
	typedef R reducer_type;
	typedef D dataset_type;
	typedef P<typename M::key_type> partitioner_type;
	typedef S<partitioner_type, typename M::value_type> result_type;

	job(result_type &r, const D &d)
	: _result(r), _dataset(d)
	{ }

	void
	exec(int ntask)
	{
		task<result_type, M, R, typename D::const_iterator> **tasks =
			new task<result_type, M, R, typename D::const_iterator> *[ntask];
		// assuming approximate equality in the processing
		// time of records follows the simple partition:
		size_t len = _dataset.size() / ntask;
		for (int i = 0; i < ntask - 1; ++i)
			tasks[i] = new task<result_type, M, R, typename D::const_iterator>
				(_result, _dataset.begin() + len * i, _dataset.begin() + len * (i + 1));
		tasks[ntask - 1] = new task<result_type, M, R, typename D::const_iterator>
			(_result, _dataset.begin() + len * (ntask - 1), _dataset.end());
		for (int i = 0; i < ntask; ++i)
			tasks[i]->start();
		for (int i = 0; i < ntask; ++i)
			delete tasks[i];
		delete [] tasks;
	}

private:
	result_type &_result;
	const D     &_dataset;
};


// a typical job with default reducer and partitioner. Yet it is still
// flexible as the += oprator of the value_type of mapper can be
// overloaded, which amounts to customizing the reducer.
template<class M, class D>
class typical_job :
	public  job<store, M, reducer<typename M::value_type>, partitioner, D> {
public:
	typedef job<store, M, reducer<typename M::value_type>, partitioner, D> job_type;	
	typedef typename job_type::mapper_type      mapper_type;
	typedef typename job_type::reducer_type     reducer_type;
	typedef typename job_type::dataset_type     dataset_type;
	typedef typename job_type::partitioner_type partitioner_type;
	typedef typename job_type::result_type      result_type;

	typical_job(result_type &r, const D &d)
	: job_type(r, d)
	{ }
};

} // namespace mapreduce

} // namespace ulib

#endif  // __ULIB_MAPREDUCE_H
