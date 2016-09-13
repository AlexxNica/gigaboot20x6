// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <string.h>

#include <utils.h>

#include <inet6.h>
#include <netifc.h>

static EFI_SIMPLE_NETWORK* snp;

#define MAX_FILTER 8
static EFI_MAC_ADDRESS mcast_filters[MAX_FILTER];
static unsigned mcast_filter_count = 0;

#define NUM_BUFFER_PAGES 8
#define ETH_BUFFER_SIZE 1516
#define ETH_HEADER_SIZE 16
#define ETH_BUFFER_MAGIC 0x424201020304A7A7UL

typedef struct eth_buffer_t eth_buffer;
struct eth_buffer_t {
    uint64_t magic;
    eth_buffer* next;
    uint8_t data[0];
};

static EFI_PHYSICAL_ADDRESS eth_buffers_base = 0;
static eth_buffer* eth_buffers = NULL;

void* eth_get_buffer(size_t sz) {
    eth_buffer* buf;
    if (sz > ETH_BUFFER_SIZE) {
        return NULL;
    }
    if (eth_buffers == NULL) {
        return NULL;
    }
    buf = eth_buffers;
    eth_buffers = buf->next;
    buf->next = NULL;
    return buf->data;
}

void eth_put_buffer(void* data) {
    eth_buffer* buf = (void*)(((uint64_t)data) & (~2047));

    if (buf->magic != ETH_BUFFER_MAGIC) {
        printf("fatal: eth buffer %p (from %p) bad magic %lx\n", buf, data, buf->magic);
        for (;;)
            ;
    }
    buf->next = eth_buffers;
    eth_buffers = buf;
}

int eth_send(void* data, size_t len) {
    EFI_STATUS r;

    if ((r = snp->Transmit(snp, 0, len, (void*)data, NULL, NULL, NULL))) {
        eth_put_buffer(data);
        return -1;
    } else {
        return 0;
    }
}

void eth_dump_status(void) {
    printf("State/HwAdSz/HdrSz/MaxSz %d %d %d %d\n",
           snp->Mode->State, snp->Mode->HwAddressSize,
           snp->Mode->MediaHeaderSize, snp->Mode->MaxPacketSize);
    printf("RcvMask/RcvCfg/MaxMcast/NumMcast %d %d %d %d\n",
           snp->Mode->ReceiveFilterMask, snp->Mode->ReceiveFilterSetting,
           snp->Mode->MaxMCastFilterCount, snp->Mode->MCastFilterCount);
    UINT8* x = snp->Mode->CurrentAddress.Addr;
    printf("MacAddr %02x:%02x:%02x:%02x:%02x:%02x\n",
           x[0], x[1], x[2], x[3], x[4], x[5]);
    printf("SetMac/MultiTx/LinkDetect/Link %d %d %d %d\n",
           snp->Mode->MacAddressChangeable, snp->Mode->MultipleTxSupported,
           snp->Mode->MediaPresentSupported, snp->Mode->MediaPresent);
}

int eth_add_mcast_filter(const mac_addr* addr) {
    if (mcast_filter_count >= MAX_FILTER)
        return -1;
    if (mcast_filter_count >= snp->Mode->MaxMCastFilterCount)
        return -1;
    memcpy(mcast_filters + mcast_filter_count, addr, ETH_ADDR_LEN);
    mcast_filter_count++;
    return 0;
}

static EFI_EVENT net_timer = NULL;

#define TIMER_MS(n) (((uint64_t)(n)) * 10000UL)

void netifc_set_timer(uint32_t ms) {
    if (net_timer == 0) {
        return;
    }
    gBS->SetTimer(net_timer, TimerRelative, TIMER_MS(ms));
}

int netifc_timer_expired(void) {
    if (net_timer == 0) {
        return 0;
    }
    if (gBS->CheckEvent(net_timer) == EFI_SUCCESS) {
        return 1;
    }
    return 0;
}

/* Search the available network interfaces via SimpleNetworkProtocol handles
 * and find the first valid one with a Link detected */
EFI_SIMPLE_NETWORK *netifc_find_available(void) {
    EFI_BOOT_SERVICES* bs = gSys->BootServices;
    EFI_STATUS ret;
    EFI_SIMPLE_NETWORK *cur_snp = NULL;
    EFI_HANDLE handles[32];
    CHAR16 *paths[32];
    size_t nic_cnt = 0;
    size_t sz = sizeof(handles);
    uint32_t last_parent = 0;
    uint32_t int_sts;
    void *tx_buf;

    /* Get the handles of all devices that provide SimpleNetworkProtocol interfaces */
    ret = bs->LocateHandle(ByProtocol, &SimpleNetworkProtocol, NULL, &sz, handles);
    if (ret != EFI_SUCCESS) {
        printf("Failed to locate network interfaces (%s)\n", efi_strerror(ret));
        return NULL;
    }

    nic_cnt = sz / sizeof(EFI_HANDLE);
    for (size_t i = 0; i < nic_cnt; i++) {
        paths[i] = HandleToString(handles[i]);
    }

    /* Iterate over our SNP list until we find one with an established link */
    for (size_t i = 0; i < nic_cnt; i++) {
         /* Check each interface once, but ignore any additional device paths a given interface
          * may provide. e1000 tends to add a path for ipv4 and ipv6 configuration information
          * for instance */
        if (i != last_parent) {
            if (memcmp(paths[i], paths[last_parent], strlen_16(paths[last_parent])) == 0) {
                continue;
            } else {
                last_parent = i;
            }
        }

        Print(L"%s: ", paths[i]);
        ret = bs->HandleProtocol(handles[i], &SimpleNetworkProtocol, (void**)&cur_snp);
        if (ret) {
            printf("Failed to open (%s)\n", efi_strerror(ret));
            continue;
        }

        /* If a driver is provided by the firmware then it should be started already, but check
         * to make sure. This also covers the case where we're providing the AX88772 driver in-line
         * during this boot itself */
        ret = cur_snp->Start(cur_snp);
        if (EFI_ERROR(ret) && ret != EFI_ALREADY_STARTED) {
            printf("Failed to start (%s)", efi_strerror(ret));
            goto link_fail;
        }

        if (ret != EFI_ALREADY_STARTED) {
            ret = cur_snp->Initialize(cur_snp, 0, 0);
            if (EFI_ERROR(ret)) {
                printf("Failed to initialize (%s)\n", efi_strerror(ret));
                goto link_fail;
            }
        }

        /* Prod the driver to cache its current status. We don't need the status or buffer,
         * but some drivers appear to require the OPTIONAL parameters. */
        ret = cur_snp->GetStatus(cur_snp, &int_sts, &tx_buf);
        if (EFI_ERROR(ret)) {
            printf("Failed to read status (%s)\n", efi_strerror(ret));
            goto link_fail;
        }

        /* With status cached, do we have a Link detected on the netifc? */
        if (!cur_snp->Mode->MediaPresent) {
            printf("No link detected\n");
            goto link_fail;
        }

        printf("Link detected!\n");
        return cur_snp;

link_fail:
        bs->CloseProtocol(handles[i], &SimpleNetworkProtocol, gImg, NULL);
        cur_snp = NULL;
    }

    return NULL;
}

int netifc_open(void) {
    EFI_BOOT_SERVICES* bs = gSys->BootServices;
    EFI_STATUS ret;
    int j;

    bs->CreateEvent(EVT_TIMER, TPL_CALLBACK, NULL, NULL, &net_timer);

    snp = netifc_find_available();
    if (!snp) {
        printf("Failed to find a usable network interface\n");
        return -1;
    }

    if (bs->AllocatePages(AllocateAnyPages, EfiLoaderData, NUM_BUFFER_PAGES, &eth_buffers_base)) {
        printf("Failed to allocate net buffers\n");
        return -1;
    }

    uint8_t* ptr = (void*)eth_buffers_base;
    for (ret = 0; ret < (NUM_BUFFER_PAGES * 2); ret++) {
        eth_buffer* buf = (void*)ptr;
        buf->magic = ETH_BUFFER_MAGIC;
        eth_put_buffer(buf);
        ptr += 2048;
    }

    ip6_init(snp->Mode->CurrentAddress.Addr);

    ret = snp->ReceiveFilters(snp,
                            EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST,
                            0, 0, mcast_filter_count, (void*)mcast_filters);
    if (ret) {
        printf("Failed to install multicast filters %s\n", efi_strerror(ret));
        return -1;
    }

    eth_dump_status();

    if (snp->Mode->MCastFilterCount != mcast_filter_count) {
        printf("OOPS: expected %d filters, found %d\n",
               mcast_filter_count, snp->Mode->MCastFilterCount);
        goto force_promisc;
    }
    for (size_t i = 0; i < mcast_filter_count; i++) {
        //uint8_t *m = (void*) &mcast_filters[i];
        //printf("i=%d %02x %02x %02x %02x %02x %02x\n", i, m[0], m[1], m[2], m[3], m[4], m[5]);
        for (j = 0; j < mcast_filter_count; j++) {
            //m = (void*) &snp->Mode->MCastFilter[j];
            //printf("j=%d %02x %02x %02x %02x %02x %02x\n", j, m[0], m[1], m[2], m[3], m[4], m[5]);
            if (!memcmp(mcast_filters + i, &snp->Mode->MCastFilter[j], 6)) {
                goto found_it;
            }
        }
        printf("OOPS: filter #%zu missing\n", i);
        goto force_promisc;
    found_it:;
    }

    return 0;

force_promisc:
    ret = snp->ReceiveFilters(snp,
                            EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS |
                                EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST,
                            0, 0, 0, NULL);
    if (ret) {
        printf("Failed to set promiscuous mode (%s)\n", efi_strerror(ret));
        return -1;
    }
    return 0;
}

void netifc_close(void) {
    gBS->SetTimer(net_timer, TimerCancel, 0);
    gBS->CloseEvent(net_timer);
    snp->Shutdown(snp);
    snp->Stop(snp);
}

int netifc_active(void) {
    return (snp != 0);
}

void netifc_poll(void) {
    UINT8 data[1514];
    EFI_STATUS r;
    UINTN hsz, bsz;
    UINT32 irq;
    VOID* txdone;

    if ((r = snp->GetStatus(snp, &irq, &txdone))) {
        return;
    }

    if (txdone) {
        eth_put_buffer(txdone);
    }

    hsz = 0;
    bsz = sizeof(data);
    r = snp->Receive(snp, &hsz, &bsz, data, NULL, NULL, NULL);
    if (r != EFI_SUCCESS) {
        return;
    }
#if TRACE
    printf("RX %02x:%02x:%02x:%02x:%02x:%02x < %02x:%02x:%02x:%02x:%02x:%02x %02x%02x %d\n",
            data[0], data[1], data[2], data[3], data[4], data[5],
            data[6], data[7], data[8], data[9], data[10], data[11],
            data[12], data[13], (int)(bsz - hsz));
#endif
    eth_recv(data, bsz);
}
