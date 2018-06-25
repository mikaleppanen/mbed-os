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

#ifndef HEAP_STATS
#define HEAP_STATS

typedef struct mem_entry {
    void *pointer;
    void *caller;
    uint16_t size;
    uint16_t timer;
} MEM_ENTRY;

#define ENTRIES             600
#define ENABLE_SNAPSHOT     100000
#define TRACE_TRESHOLD      100000

extern mem_entry mem_entries[];
extern mem_entry mem_entries_snapshot[];

void order_mem_entries(mem_entry *entries);
void add_mem_entry(void *ptr, void *caller, int size);
void free_mem_entry(void *ptr, void *caller);
void time_mem_entry(void);
extern "C" void print_mem_entry(mem_entry *entries);
extern "C" void mbed_mem_trace_callback(uint8_t op, void *res, void *caller, ...);

#endif /* HEAP_STATS */
