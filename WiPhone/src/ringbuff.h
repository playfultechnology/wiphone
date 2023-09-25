/*
Copyright © 2019, 2020, 2021, 2022 HackEDA, Inc.
Licensed under the WiPhone Public License v.1.0 (the "License"); you
may not use this file except in compliance with the License. You may
obtain a copy of the License at
https://wiphone.io/WiPhone_Public_License_v1.0.txt.

Unless required by applicable law or agreed to in writing, software,
hardware or documentation distributed under the License is distributed
on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/

#ifndef _RING_BUFFER_
#define _RING_BUFFER_

/*
 * Slightly more capable circular buffer.
 * 
 * Copyright (c) 2018, Phillip Johnston, CC0-1.0
 *           https://embeddedartistry.com/blog/2017/4/6/circular-buffers-in-cc
 * Copyright (c) 2019, ESP32 WiPhone project, by Andriy Makukha
 *           std::mutex -> atomic test-and-set lock (std::mutex is unreliable via FreeRTOS on ESP32)
 *           added methods getCopy(), forcePut(), zero(), operator[]
 */

#include <memory>
#include <stdatomic.h>

template <class T>
class RingBuffer {
  public:
    explicit RingBuffer(size_t size) :
        buf_(std::unique_ptr<T[]>(new T[size])),
        max_size_(size)
    { 
        atomic_flag_clear(&lock_);
    };

    void put(T item);               // add element if not full
    void forcePut(T item);          // add element even if full
    T get();                        // pop element (FIFO)
    T* getCopy();                   // get linear copy of the array
    void getCopy(T* out);
    T operator[](int index);        // rbuff[-1] - last element written; TODO: why cannot make it const?
    void reset();                   // clear the buffer
    void zero();                    // set all bytes to zero
    bool empty() const;
    bool full() const;
    size_t capacity() const;
    size_t size() const;

  private:
    std::unique_ptr<T[]> buf_;
    atomic_flag lock_;
    size_t write_ = 0;
    size_t read_ = 0;
    const size_t max_size_;
    bool full_ = false;
    inline void cinc(size_t& x) { x = (x + 1) % max_size_; };           // circular increment
};

template<class T>
void RingBuffer<T>::zero() {
    while (atomic_flag_test_and_set(&lock_) == 1);
    memset(buf_.get(), 0, max_size_*sizeof(T));
    atomic_flag_clear(&lock_);
}

template<class T>
void RingBuffer<T>::reset() {
    while (atomic_flag_test_and_set(&lock_) == 1);

    write_ = read_;
    full_ = false;

    atomic_flag_clear(&lock_);
}

template<class T>
bool RingBuffer<T>::empty() const {
    //if head and tail are equal, we are empty
    return (!full_ && (write_ == read_));
}

template<class T>
bool RingBuffer<T>::full() const {
    //If tail is ahead the head by 1, we are full
    return full_;
}

template<class T>
size_t RingBuffer<T>::capacity() const {
    return max_size_;
}

template<class T>
size_t RingBuffer<T>::size() const {
    size_t size = max_size_;

    if(!full_) {
        if(write_ >= read_)
            size = write_ - read_;
        else
            size = max_size_ + write_ - read_;
    }

    return size;
}

template<class T>
void RingBuffer<T>::put(T item) {
        while (atomic_flag_test_and_set(&lock_) == 1);
        if (!full_) {
                buf_[write_] = item;
                cinc(write_);
                if (write_ == read_) {
                        full_ = true;
                }
        }
        atomic_flag_clear(&lock_);
}

template<class T>
void RingBuffer<T>::forcePut(T item) {
        while (atomic_flag_test_and_set(&lock_) == 1);
        if (full_) cinc(read_);
        buf_[write_] = item;
        cinc(write_);
        if (write_ == read_)
            full_ = true;
        atomic_flag_clear(&lock_);
}

template<class T>
T RingBuffer<T>::get() {
    while (atomic_flag_test_and_set(&lock_) == 1);

    if(empty()) {
        atomic_flag_clear(&lock_);
        return T();
    }

    auto val = buf_[read_];
    cinc(read_);
    full_ = false;

    atomic_flag_clear(&lock_);
    return val;
}

template<class T>
T* RingBuffer<T>::getCopy() {
        while (atomic_flag_test_and_set(&lock_) == 1);

        size_t current_size = this->size();
        T* d = (T*) malloc(sizeof(T)*(current_size + 1));
        if (current_size>0) {
                if (read_ < write_) {
                        memcpy(d, buf_.get()+read_, current_size*sizeof(T)); 
                } else {
                        memcpy(d, buf_.get()+read_, (max_size_-read_)*sizeof(T));
                        if (write_) memcpy(d+max_size_-read_, buf_.get(), write_*sizeof(T));
                }
        }
        d[current_size] = T(0);

        atomic_flag_clear(&lock_);
        return d;
}

template<class T>
void RingBuffer<T>::getCopy(T* out) {
        while (atomic_flag_test_and_set(&lock_) == 1);

        size_t current_size = this->size();
        if (current_size>0) {
                if (read_ < write_) {
                        memcpy(out, buf_.get()+read_, current_size*sizeof(T)); 
                } else {
                        memcpy(out, buf_.get()+read_, (max_size_-read_)*sizeof(T));
                        if (write_) memcpy(out+max_size_-read_, buf_.get(), write_*sizeof(T));
                }
        }
        out[current_size] = T(0);

        atomic_flag_clear(&lock_);
}

template<class T>
T RingBuffer<T>::operator[](int index) {

    while (atomic_flag_test_and_set(&lock_) == 1);

    if(empty()) {
        atomic_flag_clear(&lock_);
        return T();
    }

    auto val = buf_[(max_size_ + write_ + index) % max_size_];

    atomic_flag_clear(&lock_);
    return val;

}

#endif // _RING_BUFFER_
