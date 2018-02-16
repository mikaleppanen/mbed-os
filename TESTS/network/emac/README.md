# Description

This document describes how to run EMAC tests. The EMAC test cases are made using Ethernet Configuration Testing Protocol (CTP). To run the tests, one device in the Ethernet segment needs to be configured to be a CTP echo server. The devices running the test cases, use the echo server to forward the CTP Ethernet frames back.

# Configuring CTP echo server

A device can be configured to be a CTP echo server by enabling `echo-server` setting in the test environment's application `json` file. When device is configured to be a CTP echo server, it starts to forward CTP messages automatically after power up and will continue forwarding until power down.

# Test cases

## EMAC initialise

Test case initializes the EMAC driver and test network stack.

EMAC test environment uses test network stack as the default network stack. Stack is enabled by setting
the `nsapi.default-stack` option in test environment's application `json` file to value `TEST`.

Test network stack is a bare minimum implementation of the network stack and consists of the functionality needed to bringup the network interface. Test network stack is defined in `emac_TestNetworkStack.h` and `emac_TestNetworkStack.cpp` files. Stack uses test memory manager as the memory manager for the EMAC. The test memory manager is defined in `emac_TestMemoryManager.h` and `emac_TestMemoryManager.cpp` files. Message buffers send to the EMAC in the `link_out()` are allocated from the test memory manager buffer pool. The test memory manager pool allocation unit (buffer size) is 610 bytes.

Initialization test constructs and connects the network interface. The test network stack and the EMAC are bind to the network interface using `get_default_instance()` calls to the stack and to the EMAC.

After construction, the network interface is connected. Connect call will trigger a bring up call to the test network stack. The bring up call triggers a call to `emac_if_init()` function in the EMAC initialization test case. 

Test case `emac_if_init()` function configures and powers up the EMAC.

Configuring includes following steps:
* setting the test memory manager as the memory manager for the EMAC.
* setting EMAC link input and state callbacks to call test environment input and state callback handlers.
* reading and setting the ethernet MAC address.

## EMAC broadcast

Sends three 100 bytes CTP broadcast messages, waits for three seconds and sends three 60 bytes CTP broadcast messages. Listens for the CTP echo server responses and stores the addresses of the echo servers if replies are received. The test case will pass if there are no responses from echo server, but further test cases will be skipped.

## EMAC unicast

Sends three 100 bytes CTP unicast messages to the CTP echo server. Verifies that all are replied. 

## EMAC unicast frame length
 
Sends CTP unicast messages with Ethernet message length from 100 bytes to maximum with 50 bytes increments. Verifies that all are replied. 

## EMAC unicast burst
 
Sends CTP unicast messages with Ethernet message length from 100 bytes to maximum with 50 bytes increments. Repeats the sending 10 times. Verifies that all are replied. 

## EMAC multicast filter
 
Tests multicast filtering. If the EMAC of the target supports multicast filtering then it shall enable `MULTICAST_FILTERING_SUPPORTED` definition at the beginning of the test module. Setting enables verification of the results.

The multicast testing is made by requesting CTP echo server to forward back the CTP messages to specified multicast address as destination address.

Test steps:

0. Verify using unicast that the echo server responses are received.
1. Set the ipv6 multicast filter address and the echo server reply (forward) address to different values. Check that the echo response is filtered.
2. Set the ipv6 multicast filter address and the echo server reply address to same value. Check that the response is not filtered.
3. Set the ipv4 multicast filter address and the echo server reply address to different values. Check that the response is filtered.
4. Set the ipv4 multicast filter address and the echo server reply address to same value. Check that the response is not filtered.
5. Enable receiving all multicasts. Check that the response is not filtered.

## EMAC memory

Tests memory manager out-of-memory situations. Test case configures the test memory manager to reject memory buffer allocations made by the EMAC. Memory buffer allocations are divided into output and input memory allocations. Output memory allocations are those that are made by the EMAC in `link_out()` function called by the network stack (test case). Input memory allocations are other memory allocations made by the EMAC. Depending on EMAC implementation it may or may not allocate memory manager buffers in the link output function. In case memory manager buffers are not allocated, disabling the link output memory allocations in the test does not affect the functionality.

In each test step test case sends CTP unicast messages with Ethernet message length from 100 bytes to the maximum with 50 bytes increments. Memory buffers sent to EMAC in `link_out()` function are forced to be non-aligned in this test case.

Test steps:

0. Memory buffer allocations are allowed. Verify that echo server responses are received.
1. Disable input memory buffer allocations. The echo server responses should not be received.
2. Allow memory buffer allocations. Verify that the echo server responses are received.
3. Disable output memory buffer allocations. The echo server responses may or may not be received depending on the EMAC link out implementation.
4. Allow memory buffer allocations. Verify that the echo server responses are received.
5. Disable input and output memory buffer allocations. The echo server responses should not be received.
6. Allow memory buffer allocations. Verify that the echo server responses are received.
7. Allocate memory buffers that are sent to the EMAC in link out from heap (contiguous memory). Verify that the echo server responses are received.

