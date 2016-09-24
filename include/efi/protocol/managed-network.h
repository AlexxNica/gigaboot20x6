// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <efi.h>

#define EFI_MANAGED_NETWORK_PROTOCOL_GUID \
    {0x7ab33a91, 0xace5, 0x4326,{0xb5, 0x72, 0xe7, 0xee, 0x33, 0xd3, 0x9f, 0x16}}

EFI_GUID ManagedNetworkProtocol = EFI_MANAGED_NETWORK_PROTOCOL_GUID;

struct _EFI_MANAGED_NETWORK_PROTOCOL;

typedef struct {
    EFI_TIME  Timestamp;
    EFI_EVENT RecycleEvent;
    UINT32 PacketLength;
    UINT32 HeaderLength;
    UINT32 AddressLength;
    UINT32 DataLength;
    BOOLEAN BroadcastFlag;
    BOOLEAN MulticastFlag;
    BOOLEAN PromiscuousFlag;
    UINT16  ProtocolType;
    VOID *DestinationAddress;
    VOID *SourceAddress;
    VOID *MediaHeader;
    VOID *PacketData;
} EFI_MANAGED_NETWORK_RECEIVE_DATA;

typedef struct {
    UINT32 FragmentLength;
    VOID *FragmentBuffer;
} EFI_MANAGED_NETWORK_FRAGMENT_DATA;

typedef struct {
    EFI_MAC_ADDRESS *DestinationAddress;
    EFI_MAC_ADDRESS *SourceAddress;
    UINT16 ProtocolType;
    UINT32 DataLength;
    UINT16 HeaderLength;
    UINT16 FragmentCount;
    EFI_MANAGED_NETWORK_FRAGMENT_DATA FragmentTable[1];
} EFI_MANAGED_NETWORK_TRANSMIT_DATA;

typedef struct {
    EFI_EVENT Event;
    EFI_STATUS Status;
    union {
        EFI_MANAGED_NETWORK_RECEIVE_DATA *RxData;
        EFI_MANAGED_NETWORK_TRANSMIT_DATA *TxData;
    } Packet;
} EFI_MANAGED_NETWORK_COMPLETION_TOKEN;

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_GET_MODE_DATA) (
    IN  struct _EFI_MANAGED_NETWORK_PROTOCOL     *This,
    OUT EFI_MANAGED_NETWORK_CONFIG_DATA *MnpConfigData OPTIONAL,
    OUT EFI_SIMPLE_NETWORK_MODE         *SnpModeData OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_CONFIGURE) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN EFI_MANAGED_NETWORK_CONFIG_DATA      *MnpConfigData OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_MCAST_IP_TO_MAC) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN BOOLEAN                              Ipv6Flag,
    IN EFI_IP_ADDRESS                       *IpAddress,
    OUT EFI_MAC_ADDRESS                     *MacAddress
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_GROUPS) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN BOOLEAN                              JoinFlag,
    IN EFI_MAC_ADDRESS                      *MacAddress OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_TRANSMIT) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN EFI_MANAGED_NETWORK_COMPLETION_TOKEN *Token
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_RECEIVE) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN EFI_MANAGED_NETWORK_COMPLETION_TOKEN *Token
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_CANCEL)(
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This,
    IN EFI_MANAGED_NETWORK_COMPLETION_TOKEN *Token OPTIONAL
);

typedef EFI_STATUS (EFIAPI *EFI_MANAGED_NETWORK_POLL) (
    IN struct _EFI_MANAGED_NETWORK_PROTOCOL *This
);

typedef struct _EFI_MANAGED_NETWORK {
    EFI_MANAGED_NETWORK_GET_MODE_DATA   GetModeData;
    EFI_MANAGED_NETWORK_CONFIGURE       Configure;
    EFI_MANAGED_NETWORK_MCAST_IP_TO_MAC McastIpToMac;
    EFI_MANAGED_NETWORK_GROUPS          Groups;
    EFI_MANAGED_NETWORK_TRANSMIT        Transmit;
    EFI_MANAGED_NETWORK_RECEIVE         Receive;
    EFI_MANAGED_NETWORK_CANCEL          Cancel;
    EFI_MANAGED_NETWORK_POLL            Poll;
} EFI_MANAGED_NETWORK;
