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

#ifndef _LINER_ARARRAY_H_
#define _LINER_ARARRAY_H_

#include "Arduino.h"
#include "helpers.h"

// TODO: use this for headers in TinySip

/*
 * Description:
 *     Dynamically allocated array that expands exponentially as more elements are added.
 *     Header-only.
 *
 *     Template parameter B specifies whether the memory should be allocated in SRAM (internal ESP32 RAM) or PSRAM (external)
 *         false - prefer internal memory (faster, but smaller)
 *         true  - prefer external memory (bigger, but slower)
 *     (This is the reason we use this class instead of standard containers like std::vector and std::list, even when it
 *     doesn't provide the optimal performance.)
 *
 * Developer notes:
 *     Beware that if you have an empty array, this won't work:
 *         LinearArray<int, false> arr;
 *         arr[0] = 1;
 *     because it will not increase array size. Correct usage is:
 *         LinearArray<int, false> arr;
 *         arr.add(1)
 */

#define LA_INTERNAL_RAM   false
#define LA_EXTERNAL_RAM   true

template <class T, bool B>
class LinearArray {

public:

  // This allows iterating over elements like:
  //     for (auto it = arr.iterator(); it.valid(); ++it) { op(*it); }
  // See this for relation to std::iterator<std::forward_iterator_tag>:
  //     https://stackoverflow.com/a/39767072/5407270

  class LinearArrayIterator {   // : public std::iterator<std::forward_iterator_tag, T, int, T*, T&>

    typedef LinearArrayIterator iterator;

  public:
    LinearArrayIterator(LinearArray<T,B>& arr) : arr_(arr), pos_(0) {}
    LinearArrayIterator(LinearArray<T,B>& arr, int i) : arr_(arr), pos_(i) {}
    ~LinearArrayIterator() {}

    iterator  operator++(int) { /* postfix */
      int p = pos_++;
      return LinearArrayIterator(arr_, p);
    }
    iterator& operator++() {  /* prefix */
      ++pos_;
      return *this;
    }
    T& operator* () const                           {
      return arr_[pos_];
    }
    T* operator->() const                           {
      return &arr_[pos_];
    }
    iterator  operator+ (int v)   const             {
      return LinearArrayIterator(arr_, pos_ + v);
    }
    operator int() const                  {
      return pos_;
    }
    bool      valid() const                         {
      return pos_ < arr_.size();
    }

  protected:
    int pos_;
    LinearArray<T,B>& arr_;
  };

  LinearArray();
  LinearArray(size_t expectedSize);
  ~LinearArray();

  /* Access interfaces */

  size_t size() const;
  size_t maxSize() const;
  T operator[](int index) const;
  LinearArrayIterator iterator() {
    return LinearArrayIterator(*this);
  }

  /* Access and modification */

  T& operator[](int index);

  /* Modification interfaces */

  bool ensure(size_t newSize);
  bool add(T element);
  bool extend(T* elements, size_t size);
  bool insert(size_t pos, T element);
  void reorderAdded(int (*cmp)(T*, T*));
  void sort(int (*cmp)(T*, T*));
  void sortFrom(int startAt, int (*cmp)(T*, T*));

  T pop();
  bool remove(size_t pos);
  bool removeByValue(T element);
  void clear();
  void purge();

protected:

  T* arrayDyn;
  size_t arraySize;
  size_t arrayAllocSize;
};

template <class T, bool B>
LinearArray<T, B>::LinearArray() : arrayDyn(nullptr), arraySize(0), arrayAllocSize(0) {};

template <class T, bool B>
LinearArray<T, B>::LinearArray(size_t expectedSize) : LinearArray() {
  ensure(expectedSize);
}

template <class T, bool B>
LinearArray<T, B>::~LinearArray() {
  this->clear();
};

/* Access interfaces */

template <class T, bool B>
size_t LinearArray<T, B>::size() const {
  return this->arraySize;
};

template <class T, bool B>
size_t LinearArray<T, B>::maxSize() const {
  return this->arrayAllocSize;
};

template <class T, bool B>
T LinearArray<T, B>::operator[](int index) const {
  return this->arrayDyn[index];
};

template <class T, bool B>
T& LinearArray<T, B>::operator[](int index) {
  return this->arrayDyn[index];
};



/* Modification interfaces */

template <class T, bool B>
bool LinearArray<T, B>::ensure(size_t newSize) {

  if (newSize > this->arrayAllocSize) {

    T* tmp = NULL;

    // More memory needs to be allocated
    if (this->arrayAllocSize > 0) {

      // Double the amount of storage until it's sufficient
      size_t newAllocSize = this->arrayAllocSize;
      while (newSize > newAllocSize) {
        newAllocSize *= 2;
      }
      tmp = (T*) wRealloc<B>(this->arrayDyn, newAllocSize * sizeof(T));
      if (tmp != NULL) {

        // Successfully allocated
        this->arrayAllocSize = newAllocSize;

      } else if (newSize > newAllocSize) {

        // Otherwise: try allocating the exact amount required
        tmp = (T*) wRealloc<B>(this->arrayDyn, newSize * sizeof(T));
        if (tmp != NULL) {
          this->arrayAllocSize = newSize;
        }

      }

    } else {

      // Initial allocation
      tmp = (T*) wMalloc<B>(newSize * sizeof(T));
      if (tmp != nullptr) {
        this->arrayAllocSize = newSize;
      }

    }

    // Logging, success/failure
    if (tmp != NULL) {
      this->arrayDyn = tmp;
      log_v("realloced: %d", this->arrayAllocSize);
    } else {
      log_v("failed to realloc: %d", newSize * sizeof(T));
      return false;
    }
  }
  return true;
}

/*
 * Description:
 *     add (push) an element to the end of the array.
 * Return:
 *     true if successful, false if the array could not be expanded.
 */
template <class T, bool B>
bool LinearArray<T, B>::add(T element) {

  // First: ensure enough memory is allocated
  if (this->ensure(this->arraySize + 1)) {

    // Second: actually save to array
    this->arrayDyn[this->arraySize++] = element;

    return true;
  }

  return false;
};

/*
 * Description:
 *     add multiple elements to the end of the array.
 * Return:
 *     true if successful, false if the array could not be expanded.
 */
template <class T, bool B>
bool LinearArray<T, B>::extend(T* elements, size_t size) {
  if (this->ensure(this->arraySize + size)) {
    // NOTE: memmove doesn't work for external memory of ESP32!
    //memmove(this->arrayDyn + this->arraySize, elements, size*sizeof(T));
    for (int i = 0; i < size; i++) {
      this->arrayDyn[this->arraySize + i] = elements[i];
    }
    this->arraySize += size;
    return true;
  }
  return false;
};

template <class T, bool B>
bool LinearArray<T, B>::insert(size_t pos, T element) {

  // First: ensure enough memory is allocated
  if (this->ensure(this->arraySize + 1) && pos <= this->arraySize) {

    // Second: shift right
    this->arraySize++;
    for (size_t j = this->arraySize - 1; j > pos; j--) {
      this->arrayDyn[j] = this->arrayDyn[j - 1];
    }

    // Third: actually insert
    this->arrayDyn[pos] = element;

    return true;
  }

  return false;
};

template <class T, bool B>
void LinearArray<T, B>::reorderAdded(int (*cmp)(T*, T*)) {
  for (size_t j = 0; j < this->arraySize - 1; j++) {
    if ((*cmp)(&this->arrayDyn[j], &this->arrayDyn[this->arraySize - 1]) < 0) {
      this->insert(j, this->pop());
      break;
    }
  }
};

template <class T, bool B>
void LinearArray<T, B>::sortFrom(int startAt, int (*cmp)(T*, T*)) {
  // C sorting
  if (startAt + 1 >= this->arraySize) {
    return;
  }
  qsort(this->arrayDyn + startAt, this->arraySize - startAt, sizeof(T), (int (*)(const void *, const void *)) cmp);
};

template <class T, bool B>
void LinearArray<T, B>::sort(int (*cmp)(T*, T*)) {
  this->sortFrom(0, cmp);
};

/*
 * Description:
 *     remove and return the last element in the array.
 */
template <class T, bool B>
T LinearArray<T, B>::pop() {
  if (this->arraySize > 0) {
    return this->arrayDyn[--this->arraySize];
  }
  return T(0);
};

/*
 * Description:
 *     remove one element from the array (shifting left the elements on the right)
 * Return:
 *     true if the element exists and was removed, false - otherwise.
 */
template <class T, bool B>
bool LinearArray<T, B>::remove(size_t pos) {
  if (pos < this->arraySize) {
    for (size_t j = pos + 1; j < this->arraySize; j++) {
      this->arrayDyn[j - 1] = this->arrayDyn[j];
    }
    this->arraySize--;
    return true;
  }
  return false;
};

/*
 * Description:
 *     find one value in the array and remove it (shifting left the elements on the right)
 * Return:
 *     true if an element was found and removed, false otherwise.
 */
template <class T, bool B>
bool LinearArray<T, B>::removeByValue(T element) {
  for (size_t i = 0; i < this->arraySize; i++) {
    if (element == this->arrayDyn[i]) {
      for (size_t j = i; j < this->arraySize - 1; j++) {
        this->arrayDyn[j] = this->arrayDyn[j + 1];
      }
      this->arraySize--;
      return true;
    }
  }
  return false;
};

/* Description:
 *     free the memory and reset the state to empty
 */
template <class T, bool B>
void LinearArray<T, B>::clear() {
  freeNull((void **) &this->arrayDyn);
  this->arraySize = 0;
  this->arrayAllocSize = 0;
}

/* Description:
 *     delete all elements but leave the memory allocated
 */
template <class T, bool B>
void LinearArray<T, B>::purge() {
  this->arraySize = 0;
}

#endif // _LINEAR_ARRAY_H_
