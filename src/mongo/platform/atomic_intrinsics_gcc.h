/*    Copyright 2012 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 * Implementation of the AtomicIntrinsics<T>::* operations for IA-32 and AMD64 systems using a
 * GCC-compatible compiler toolchain.
 */

#pragma once

#include <boost/utility.hpp>

namespace mongo {

    /**
     * Instantiation of AtomicIntrinsics<> for all word types T where sizeof<T> <= sizeof(void *).
     *
     * On 32-bit systems, this handles 8-, 16- and 32-bit word types.  On 64-bit systems,
     * it handles 8-, 16, 32- and 64-bit types.
     */
    template <typename T, typename IsTLarge=void>
    class AtomicIntrinsics {
    public:

        static T compareAndSwap(volatile T* dest, T expected, T newValue) {

            T result;
            #if defined(__arm__)
              result = __sync_val_compare_and_swap(dest, expected, newValue);
            #else
            asm volatile ("lock cmpxchg %[src], %[dest]"
                          : [dest] "+m" (*dest),
                            "=a" (result)
                          : [src] "r" (newValue),
                            "a" (expected)
                          : "memory", "cc");
            #endif
            return result;
        }

        static T swap(volatile T* dest, T newValue) {

            T result = newValue;
            #if defined(__arm__)
              result = __sync_lock_test_and_set(dest, newValue);
              __sync_lock_release(dest); 
            #else
            // No need for "lock" prefix on "xchg".
            asm volatile ("xchg %[r], %[dest]"
                          : [dest] "+m" (*dest),
                            [r] "+r" (result)
                          :
                          : "memory");
            #endif
            return result;
        }

        static T load(volatile const T* value) {
            #if defined(__arm__)
              T result;
              __sync_synchronize();
              result = *value;
              __sync_synchronize();
            #else
            asm volatile ("mfence" ::: "memory");
            T result = *value;
            asm volatile ("mfence" ::: "memory");
            #endif
            return result;
        }

        static T loadRelaxed(volatile const T* value) {
            return *value;
        }

        static void store(volatile T* dest, T newValue) {
            #if defined(__arm__)
              __sync_synchronize();
              *dest = newValue;
              __sync_synchronize();
            #else
            asm volatile ("mfence" ::: "memory");
            *dest = newValue;
            asm volatile ("mfence" ::: "memory");
            #endif
        }

        static T fetchAndAdd(volatile T* dest, T increment) {

            T result = increment;
            #if defined(__arm__)
              result = __sync_fetch_and_add(dest, increment);
            #else
            asm volatile ("lock xadd %[src], %[dest]"
                          : [dest] "+m" (*dest),
                            [src] "+r" (result)
                          :
                          : "memory", "cc");
            #endif
            return result;
        }

    private:
        AtomicIntrinsics();
        ~AtomicIntrinsics();
    };

    /**
     * Instantiation of AtomicIntrinsics<T> where sizeof<T> exceeds sizeof(void*).
     *
     * On 32-bit systems, this handles the 64-bit word type.  Not used on 64-bit systems.
     *
     * Note that the implementations of swap, store and fetchAndAdd spin until they succeed.  This
     * implementation is thread-safe, but may have poor performance in high-contention environments.
     * However, no superior solution exists for IA-32 (32-bit x86) systems.
     */
    template <typename T>
    class AtomicIntrinsics<T, typename boost::disable_if_c<sizeof(T) <= sizeof(void*)>::type> {
    public:
        static T compareAndSwap(volatile T* dest, T expected, T newValue) {
            T result = expected;
            #if defined(__arm__)
              result = __sync_val_compare_and_swap(dest, expected, newValue);
            #else
            asm volatile ("push %%eax\n"
                          "push %%ebx\n"
                          "push %%ecx\n"
                          "push %%edx\n"
                          "mov (%%edx), %%ebx\n"
                          "mov 4(%%edx), %%ecx\n"
                          "mov (%%edi), %%eax\n"
                          "mov 4(%%edi), %%edx\n"
                          "lock cmpxchg8b (%%esi)\n"
                          "mov %%eax, (%%edi)\n"
                          "mov %%edx, 4(%%edi)\n"
                          "pop %%edx\n"
                          "pop %%ecx\n"
                          "pop %%ebx\n"
                          "pop %%eax\n"
                          :
                          : "S" (dest),
                            "D" (&result),
                            "d" (&newValue)
                          : "memory", "cc");
            #endif
            return result;
        }

        static T swap(volatile T* dest, T newValue) {

        T expected;
        T actual;
            do {
                expected = *dest;
                actual = compareAndSwap(dest, expected, newValue);
            } while (actual != expected);
            return actual;
        }

        static T load(volatile const T* value) {
            return compareAndSwap(const_cast<volatile T*>(value), T(0), T(0));
        }

        static void store(volatile T* dest, T newValue) {
            swap(dest, newValue);
        }

        static T fetchAndAdd(volatile T* dest, T increment) {

            T expected;
            T actual;
            do {
                expected = load(dest);
                actual = compareAndSwap(dest, expected, expected + increment);
            } while (actual != expected);
            return actual;
        }

    private:
        AtomicIntrinsics();
        ~AtomicIntrinsics();
    };

}  // namespace mongo
