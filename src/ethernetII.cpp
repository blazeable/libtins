/*
 * libtins is a net packet wrapper library for crafting and
 * interpreting sniffed packets.
 *
 * Copyright (C) 2011 Nasel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cassert>
#include <cstring>
#include <stdexcept>
#ifndef WIN32
    #include <net/ethernet.h>
    #include <netpacket/packet.h>
#endif
#include "ethernetII.h"
#include "rawpdu.h"
#include "ip.h"
#include "arp.h"
#include "utils.h"

const uint8_t* Tins::EthernetII::BROADCAST = (const uint8_t*)"\xff\xff\xff\xff\xff\xff";
const uint32_t Tins::EthernetII::ADDR_SIZE;

Tins::EthernetII::EthernetII(const std::string& iface, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) throw (std::runtime_error) : PDU(ETHERTYPE_IP, child) {
    memset(&_eth, 0, sizeof(ethhdr));
    if(dst_hw_addr)
        this->dst_addr(dst_hw_addr);
    if(src_hw_addr)
        this->src_addr(src_hw_addr);
    this->iface(iface);
    this->_eth.payload_type = 0;

}

Tins::EthernetII::EthernetII(uint32_t iface_index, const uint8_t* dst_hw_addr, const uint8_t* src_hw_addr, PDU* child) : PDU(ETHERTYPE_IP, child) {
    memset(&_eth, 0, sizeof(ethhdr));
    if(dst_hw_addr)
        this->dst_addr(dst_hw_addr);
    if(src_hw_addr)
        this->src_addr(src_hw_addr);
    this->iface(iface_index);
    this->_eth.payload_type = 0;
}

Tins::EthernetII::EthernetII(const uint8_t *buffer, uint32_t total_sz) : PDU(ETHERTYPE_IP) {
    if(total_sz < sizeof(ethhdr))
        throw std::runtime_error("Not enough size for an ethernetII header in the buffer.");
    memcpy(&_eth, buffer, sizeof(ethhdr));
    PDU *next = 0;
    buffer += sizeof(ethhdr);
    total_sz -= sizeof(ethhdr);
    if(total_sz) {
        switch(payload_type()) {
            case ETHERTYPE_IP:
                next = new Tins::IP(buffer, total_sz);
                break;
            case ETHERTYPE_ARP:
                next = new Tins::ARP(buffer, total_sz);
                break;
            // Other protos plz
        }
        inner_pdu(next);
    }
}

void Tins::EthernetII::dst_addr(const uint8_t* new_dst_mac) {
    memcpy(_eth.dst_mac, new_dst_mac, sizeof(_eth.dst_mac));
}

void Tins::EthernetII::src_addr(const uint8_t* new_src_mac) {
    memcpy(_eth.src_mac, new_src_mac, sizeof(_eth.src_mac));
}

void Tins::EthernetII::iface(uint32_t new_iface_index) {
    _iface_index = new_iface_index;
}

void Tins::EthernetII::iface(const std::string& new_iface) throw (std::runtime_error) {
    if (!Tins::Utils::interface_id(new_iface, this->_iface_index)) {
        throw std::runtime_error("Invalid interface name!");
    }
}

void Tins::EthernetII::payload_type(uint16_t new_payload_type) {
    this->_eth.payload_type = Utils::net_to_host_s(new_payload_type);
}

uint32_t Tins::EthernetII::header_size() const {
    return sizeof(ethhdr);
}

bool Tins::EthernetII::send(PacketSender* sender) {
    struct sockaddr_ll addr;

    memset(&addr, 0, sizeof(struct sockaddr_ll));

    addr.sll_family = Utils::net_to_host_s(PF_PACKET);
    addr.sll_protocol = Utils::net_to_host_s(ETH_P_ALL);
    addr.sll_halen = ADDR_SIZE;
    addr.sll_ifindex = _iface_index;
    memcpy(&(addr.sll_addr), _eth.dst_mac, ADDR_SIZE);

    return sender->send_l2(this, (struct sockaddr*)&addr, (uint32_t)sizeof(addr));
}

bool Tins::EthernetII::matches_response(uint8_t *ptr, uint32_t total_sz) {
    if(total_sz < sizeof(ethhdr))
        return false;
    ethhdr *eth_ptr = (ethhdr*)ptr;
    if(!memcmp(eth_ptr->dst_mac, _eth.src_mac, ADDR_SIZE)) {
        // chequear broadcast en destino original...
        return (inner_pdu()) ? inner_pdu()->matches_response(ptr + sizeof(_eth), total_sz - sizeof(_eth)) : true;
    }
    return false;
}

void Tins::EthernetII::write_serialization(uint8_t *buffer, uint32_t total_sz, const PDU *parent) {
    uint32_t my_sz = header_size();
    assert(total_sz >= my_sz);

    /* Inner type defaults to IP */
    if ((_eth.payload_type == 0) && inner_pdu()) {
        uint16_t type = ETHERTYPE_IP;
        switch (inner_pdu()->pdu_type()) {
            case PDU::IP:
                type = ETHERTYPE_IP;
                break;
            case PDU::ARP:
                type = ETHERTYPE_ARP;
                break;
            default:
                type = 0;
        }
        _eth.payload_type = Utils::net_to_host_s(type);
    }
    memcpy(buffer, &_eth, sizeof(ethhdr));
}

Tins::PDU *Tins::EthernetII::recv_response(PacketSender *sender) {
    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(struct sockaddr_ll));

    addr.sll_family = Utils::net_to_host_s(PF_PACKET);
    addr.sll_protocol = Utils::net_to_host_s(ETH_P_ALL);
    addr.sll_halen = ADDR_SIZE;
    addr.sll_ifindex = _iface_index;
    memcpy(&(addr.sll_addr), _eth.dst_mac, ADDR_SIZE);

    return sender->recv_l2(this, (struct sockaddr*)&addr, (uint32_t)sizeof(addr));
}

Tins::PDU *Tins::EthernetII::clone_packet(const uint8_t *ptr, uint32_t total_sz) {
    if(total_sz < sizeof(_eth))
        return 0;
    PDU *child = 0, *cloned;
    if(total_sz > sizeof(_eth)) {
        if((child = PDU::clone_inner_pdu(ptr + sizeof(_eth), total_sz - sizeof(_eth))) == 0)
            return 0;
    }
    cloned = new EthernetII(ptr, std::min(total_sz, (uint32_t)sizeof(_eth)));
    cloned->inner_pdu(child);
    return cloned;
}

void Tins::EthernetII::copy_fields(const EthernetII *other) {
    memcpy(&_eth, &other->_eth, sizeof(_eth));
    _iface_index = other->_iface_index;
}

Tins::PDU *Tins::EthernetII::clone_pdu() const {
    EthernetII *new_pdu = new EthernetII(_iface_index);
    new_pdu->copy_fields(this);
    return new_pdu;
}
