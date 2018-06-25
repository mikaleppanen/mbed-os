/*
 * Copyright (c) 2018 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "ns_types.h"

#include "mbed_mem_trace.h"
#include "mbed_stats.h"

#if defined (__ICCARM__)
#include "mallocstats.h"
#endif

#if defined(TOOLCHAIN_GCC)
#include "malloc.h"
#endif

#include "heap_stats.h"

/*
 * Set print_mem_entry(mem_entries_snapshot) or print_mem_entry(mem_entries) for result prints
 *
 * Define ENTRIES to needed value
 *
 * Set callback "mbed_mem_trace_set_callback(mbed_mem_trace_callback)" e.g. in malloc wrapper
 *
 *       extern "C" void* malloc_wrapper(size_t size, void* caller) {
 *           ........
 *           static bool callback_set = false;
 *           if (!callback_set) {
 *               extern void mbed_mem_trace_callback(uint8_t op, void *res, void *caller, ...);
 *               mbed_mem_trace_set_callback(mbed_mem_trace_callback);
 *               callback_set = true;
 *           }
 *           .........
 *
 * Enable mem tracing:
 *
 *     -DMBED_HEAP_STATS_ENABLED=1
 *     -DMBED_MEM_TRACING_ENABLED
 *
 * Call time value set e.g. from some timer loop
 *
 *     time_mem_entry()
 *
 * Output will be in format pointer, caller, size, elapsed time:
 *
 *     P: 0x20015b68 C: 0x801a1f1 S: 1200 T: 1005
 *     P: 0x20016028 C: 0x80096d9 S: 2 T: 999
 *     P: 0x20016038 C: 0x800afa3 S: 4 T: 999
 *     P: 0x20016048 C: 0x806573d S: 2528 T: 1005
 *     P: 0x20016a38 C: 0x806778d S: 1024 T: 1005
 *
 *
 * Use e.g. addr2line to solve caller addresses
 *
 * bash script:
 *
 * ####################################
 * #!/usr/bin/bash
 * filename="$1"
 * addrtoline="$2"
 *
 * echo "POINTER|SIZE|TIME|CALLER"
 *
 * while read -r line
 * do
 *     name="$line"
 *
 *     num='([0-9^xabcdef]+)'
 *     nonum='[^0-9^]+'
 *
 *     #P: 0x2002a6a0 C: 0x8072ba3 S: 324 T: 0
 *
 *     if [[ $name =~ $nonum$num$nonum$num$nonum$num$nonum$num ]] ; then
 *         pointer=${BASH_REMATCH[1]}
 *         caller=${BASH_REMATCH[2]}
 *         size=${BASH_REMATCH[3]}
 *         time=${BASH_REMATCH[4]}
 *
 *         code=$("$addrtoline" -e mbed-os-example-client.elf -a $caller -f -p)
 *
 *         line=$(echo "${pointer}|${size}|${time}|${code}")
 *
 *         line_final=$(echo "$line"|tr '\n\r' '  ')
 *
 *         echo $line_final
 *     fi
 *
 * done < "$filename"
 * ####################################
 *
 *
 * Use e.g. like this:
 *
 *  ./addr_converted.sh heap_dump.txt "C:\GNU Tools ARM Embedded\5.4 2016q3\bin\arm-none-eabi-addr2line.exe" >> result.txt
 *
 */


mem_entry mem_entries[ENTRIES];
mem_entry mem_entries_snapshot[ENTRIES];

int max_entries = 0;
int double_free = 0;
int size_zero = 0;
int allocated_size = 0;
int max_allocated_size = 0;
bool enable_mutex = false;

rtos::Mutex mem_entry_mutex;

mem_entry *ordered_mem_entries[ENTRIES];

void order_mem_entries(mem_entry *entries) {

    int lowest_index = -1;
    unsigned int current_entry = 0;
    int index = 0;

    while (true) {

        unsigned int lowest_entry = 0xFFFFFFFF;

        for (int i = 0; i < ENTRIES; i++) {

            if (entries[i].pointer) {

                unsigned int entry = (unsigned int) entries[i].pointer;

                if (entry <= lowest_entry && entry > current_entry) {
                    lowest_entry = entry;
                    lowest_index = i;
                }
            }
        }

        if (lowest_entry == 0xFFFFFFFF) {
            break;
        }

        current_entry = lowest_entry;

        ordered_mem_entries[index]= &(entries[lowest_index]);
        index++;

    }

    for (int i = index; i < ENTRIES; i++) {
        ordered_mem_entries[i] = NULL;
    }
}

void add_mem_entry(void *ptr, void *caller, int size)
{
    if (size == 0) {
        MBED_ASSERT(0);
    }

    if (!ptr) {
        MBED_ASSERT(0);
    }

    if (enable_mutex) {
        mem_entry_mutex.lock();
    }


    int index = -1;
    int entries = 0;

    for (int i = 0; i < ENTRIES; i++) {

        if (mem_entries[i].pointer) {
            if (mem_entries[i].pointer == ptr) {
                MBED_ASSERT(0);
            }
            entries++;
        } else if (index == -1) {
            index = i;
        }

    }

    if (index == -1) {
        MBED_ASSERT(0);
    }

    if (entries > max_entries) {
        max_entries = entries;
    }

    mem_entries[index].pointer = ptr;
    mem_entries[index].caller = caller;
    mem_entries[index].size = size;
    mem_entries[index].timer = 0;

    allocated_size += size;

    if (allocated_size > ENABLE_SNAPSHOT && allocated_size > max_allocated_size /* + hysteresis */) {
        max_allocated_size = allocated_size;
        memcpy(mem_entries_snapshot, mem_entries, sizeof(mem_entries_snapshot));
    }

    if (enable_mutex) {
        mem_entry_mutex.unlock();
    }
}

void free_mem_entry(void *ptr, void *caller)
{
    if (!ptr) {
        return;
    }

    if (enable_mutex) {
        mem_entry_mutex.lock();
    }

    int index = -1;

    for (int i = 0; i < ENTRIES; i++) {

        if (mem_entries[i].pointer && mem_entries[i].pointer == ptr) {

            if (index != -1) {
                MBED_ASSERT(0);
            }

            index = i;
        }

    }

    if (index == -1) {
        MBED_ASSERT(0);
    }

    allocated_size -= mem_entries[index].size;

    mem_entries[index].pointer = 0;
    mem_entries[index].caller = 0;
    mem_entries[index].size = 0;
    mem_entries[index].timer = 0;

    if (enable_mutex) {
        mem_entry_mutex.unlock();
    }
}

void time_mem_entry(void)
{
    if (enable_mutex) {
        mem_entry_mutex.lock();
    }

    for (int i = 0; i < ENTRIES; i++) {
        if (mem_entries[i].pointer) {
            mem_entries[i].timer++;
        }
    }

    if (enable_mutex) {
        mem_entry_mutex.unlock();
    }
}

extern "C" void print_mem_entry(mem_entry *entries)
{
    if (enable_mutex) {
        mem_entry_mutex.lock();
    }

    order_mem_entries(entries);

#if defined (__ICCARM__)
    struct mallinfo m;
    m = __iar_dlmallinfo();

    printf("non-mmapped space allocated from system %u\n", m.arena);
    printf("number of free chunks %u\n", m.ordblks);
    printf("space in mmapped regions %u\n", m.hblkhd);
    printf("maximum total allocated space %u\n", m.usmblks);
    printf("total allocated space %u\n", m.uordblks);
    printf("total free space %u\n", m.fordblks);
    printf("releasable (via malloc_trim) space %u\n", m.keepcost);
    printf("\n\n");
#endif

#if defined(TOOLCHAIN_GCC)
    printf("\nGCC\n");

    struct mallinfo m = mallinfo();

    printf("non-mmapped space allocated from system %u\n", m.arena);
    printf("number of free chunks %u\n", m.ordblks);
    printf("number of fastbin blocks %u\n", m.smblks);
    printf("number of mmapped regions %u\n", m.hblks);
    printf("space in mmapped regions %u\n", m.hblkhd);
    printf("maximum total allocated space %u\n", m.usmblks);
    printf("space available in freed fastbin blocks %u\n", m.fsmblks);
    printf("total allocated space %u\n", m.uordblks);
    printf("total free space %u\n", m.fordblks);
#endif

    printf("\nMBED\n");

    mbed_stats_heap_t heap_stats;
    mbed_stats_heap_get(&heap_stats);

    printf("Bytes allocated currently %u\n", heap_stats.current_size);
    printf("Max bytes allocated at a given time %u\n", heap_stats.max_size);
    printf("Cumulative sum of bytes ever allocated %u\n", heap_stats.total_size);
    printf("Current number of bytes allocated for the heap %u\n", heap_stats.reserved_size);
    printf("Current number of allocations %u\n", heap_stats.alloc_cnt);
    printf("Number of failed allocations %u\n", heap_stats.alloc_fail_cnt);


    for (int i = 0; i < ENTRIES; i++) {
        if (ordered_mem_entries[i]) {
            printf("P: %p C: %p S: %u T: %u\n",
                ordered_mem_entries[i]->pointer,
                ordered_mem_entries[i]->caller,
                ordered_mem_entries[i]->size,
                ordered_mem_entries[i]->timer);
        }
    }

    wait(2.0);

    if (enable_mutex) {
        mem_entry_mutex.unlock();
    }

}

extern "C" void mbed_mem_trace_callback(uint8_t op, void *res, void *caller, ...) {

    va_list va;
    size_t temp_s1, temp_s2;
    void *temp_ptr;

    va_start(va, caller);
    switch(op) {
        case MBED_MEM_TRACE_MALLOC:
            {
            temp_s1 = va_arg(va, size_t);

            if (res == 0x00) {
                print_mem_entry(mem_entries);
            }

            add_mem_entry(res, caller, temp_s1);

            mbed_stats_heap_t heap_stats;
            mbed_stats_heap_get(&heap_stats);

            if (heap_stats.current_size > TRACE_TRESHOLD) {
                print_mem_entry(mem_entries_snapshot);
                MBED_ASSERT(0);
            }
            }
            break;

        case MBED_MEM_TRACE_REALLOC:

            temp_ptr = va_arg(va, void*);
            temp_s1 = va_arg(va, size_t);

            if (res == 0x00) {
                print_mem_entry(mem_entries);
            }

            if (res) {
                free_mem_entry(temp_ptr, caller);
                add_mem_entry(res, caller, temp_s1);
            }
            break;

        case MBED_MEM_TRACE_CALLOC:
            {
            temp_s1 = va_arg(va, size_t);
            temp_s2 = va_arg(va, size_t);

            if (res == 0x00) {
                print_mem_entry(mem_entries);
            }

            add_mem_entry(res, caller, temp_s1 * temp_s2);

            mbed_stats_heap_t heap_stats;
            mbed_stats_heap_get(&heap_stats);

            if ( heap_stats.current_size > TRACE_TRESHOLD) {
                print_mem_entry(mem_entries_snapshot);
                MBED_ASSERT(0);
            }
            }
            break;

        case MBED_MEM_TRACE_FREE:

            temp_ptr = va_arg(va, void*);

            free_mem_entry(temp_ptr, caller);

            break;

        default:
            MBED_ASSERT(0);
    }

    va_end(va);

}

extern "C" void mbed_main() {

    enable_mutex = true;
}
