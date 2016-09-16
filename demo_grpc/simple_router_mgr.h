/*******************************************************************************
 * BAREFOOT NETWORKS CONFIDENTIAL & PROPRIETARY
 *
 * Copyright (c) 2015-2016 Barefoot Networks, Inc.
 *
 * All Rights Reserved.
 *
 * NOTICE: All information contained herein is, and remains the property of
 * Barefoot Networks, Inc. and its suppliers, if any. The intellectual and
 * technical concepts contained herein are proprietary to Barefoot Networks,
 * Inc.
 * and its suppliers and may be covered by U.S. and Foreign Patents, patents in
 * process, and are protected by trade secret or copyright law.
 * Dissemination of this information or reproduction of this material is
 * strictly forbidden unless prior written permission is obtained from
 * Barefoot Networks, Inc.
 *
 * No warranty, explicit or implicit is provided, unless granted under a
 * written agreement with Barefoot Networks, Inc.
 *
 ******************************************************************************/

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#pragma once

#include <PI/pi.h>

#include <boost/asio.hpp>

#include <grpc++/grpc++.h>

#include "pi.grpc.pb.h"

#include <iostream>
#include <cstring>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::CompletionQueue;

struct __attribute__((packed)) cpu_header_t {
  char zeros[8];
  uint16_t reason;
  uint16_t port;
};

struct __attribute__((packed)) arp_header_t {
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_addr_len;
  uint8_t proto_addr_len;
  uint16_t opcode;
  unsigned char hw_src_addr[6];
  uint32_t proto_src_addr;
  unsigned char hw_dst_addr[6];
  uint32_t proto_dst_addr;
};

struct __attribute__((packed)) eth_header_t {
  unsigned char dst_addr[6];
  unsigned char src_addr[6];
  uint16_t ethertype;
};

struct __attribute__((packed)) ipv4_header_t {
  unsigned char noise[16];
  uint32_t dst_addr;
};

class SimpleRouterMgr;

class PIAsyncClient {
 public:
  PIAsyncClient(SimpleRouterMgr *simple_router_mgr,
                std::shared_ptr<Channel> channel);

  void sub_packet_in();

 private:
  // struct for keeping state and data information
  class AsyncRecvPacketInState {
   public:
    AsyncRecvPacketInState(SimpleRouterMgr *simple_router_mgr,
                           pirpc::PI::Stub *stub_, CompletionQueue *cq);

    void proceed(bool ok);

   private:
    SimpleRouterMgr *simple_router_mgr{nullptr};

    enum class State {CREATE, PROCESS, FINISH};
    State state;

    // Container for the data we expect from the server.
    pirpc::PacketIn packet_in;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // Storage for the status of the RPC upon completion.
    Status status;

    std::unique_ptr<grpc::ClientAsyncReader<pirpc::PacketIn> > response_reader;
  };

  void AsyncRecvPacketIn();

  SimpleRouterMgr *simple_router_mgr{nullptr};
  std::unique_ptr<pirpc::PI::Stub> stub_;
  CompletionQueue cq_;
  std::thread recv_thread;
};

class SimpleRouterMgr {
 public:
  friend class PacketHandler;
  friend class CounterQueryHandler;
  friend class ConfigUpdateHandler;

  typedef std::vector<char> Packet;

  static void init(size_t num_devices, std::shared_ptr<Channel> channel);

  SimpleRouterMgr(int dev_id, pi_p4info_t *p4info,
                  boost::asio::io_service &io_service,
                  std::shared_ptr<Channel> channel);

  ~SimpleRouterMgr();

  int assign();

  int add_route(uint32_t prefix, int pLen, uint32_t nhop, uint16_t port,
                uint64_t *handle);

  int set_default_entries();
  int static_config();

  void add_iface(uint16_t port_num, uint32_t ip_addr,
                 const unsigned char (&mac_addr)[6]);

  int query_counter(const std::string &counter_name, size_t index,
                    uint64_t *packets, uint64_t *bytes);

  int update_config(const std::string &config_buffer);

  void start_processing_packets();

  template <typename E> void post_event(E &&event) {
    io_service.post(std::move(event));
  }

 private:
  struct Iface {
    uint16_t port_num;
    uint32_t ip_addr;
    unsigned char mac_addr[6];
    uint64_t h;

    static Iface make(uint16_t port_num, uint32_t ip_addr,
                      const unsigned char (&mac_addr)[6]) {
      Iface iface;
      iface.port_num = port_num;
      iface.ip_addr = ip_addr;
      memcpy(iface.mac_addr, mac_addr, sizeof(mac_addr));
      iface.h = 0;
      return iface;
    }
  };

  enum class UpdateMode {
    CONTROLLER_STATE,
    DEVICE_STATE
  };

  typedef std::vector<Packet> PacketQueue;

  void handle_arp(const arp_header_t &arp_header);
  void handle_ip(Packet &&pkt_copy, uint32_t dst_addr);

  int assign_mac_addr(uint16_t port, const unsigned char (&mac_addr)[6],
                      uint64_t *handle);
  int add_arp_entry(uint32_t addr, const unsigned char (&mac_addr)[6],
                    uint64_t *handle);
  void handle_arp_request(const arp_header_t &arp_header);
  void handle_arp_reply(const arp_header_t &arp_header);
  void send_arp_request(uint16_t port, uint32_t dst_addr);

  int add_one_entry(pi_p4_id_t t_id,
                    pirpc::TableMatchEntry *match_action_entry,
                    uint64_t *handle);

  int set_one_default_entry(pi_p4_id_t t_id,
                            pirpc::ActionData *action_entry);

  int add_route_(uint32_t prefix, int pLen, uint32_t nhop, uint16_t port,
                 uint64_t *handle, UpdateMode udpate_mode);

  void add_iface_(uint16_t port_num, uint32_t ip_addr,
                  const unsigned char (&mac_addr)[6], UpdateMode update_mode);

  int static_config_(UpdateMode update_mode);

  int query_counter_(const std::string &counter_name, size_t index,
                     pirpc::CounterData *counter_data);

  int update_config_(const std::string &config_buffer);

  void send_packetout(const char *data, size_t size);

  bool assigned{false};
  std::vector<Iface> ifaces;
  std::unordered_map<uint32_t, uint16_t> next_hops;
  std::unordered_map<uint32_t, PacketQueue> packet_queues;
  int dev_id;
  pi_p4info_t *p4info{nullptr};
  boost::asio::io_service &io_service;
  std::unique_ptr<pirpc::PI::Stub> stub_;
  PIAsyncClient async_client;
};