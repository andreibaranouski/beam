// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "p2p/protocol.h"
#include "p2p/connection.h"
#define LOG_DEBUG_ENABLED 1

#include "utility/bridge.h"
#include "utility/logger.h"
#include "utility/io/tcpserver.h"
#include "core/proto.h"
#include "utility/io/timer.h"
#include <boost/intrusive/set.hpp>
#include "wallet.h"

namespace beam
{
    namespace bi = boost::intrusive;

    enum WalletNetworkMessageCodes : uint8_t
    {
        senderInvitationCode     = 100,
        senderConfirmationCode   ,
        receiverConfirmationCode ,
        receiverRegisteredCode   ,
        failedCode
    };

    class WalletNetworkIO : public IErrorHandler
                          , public NetworkIOBase
    {
        struct ConnectionInfo;
        using ConnectCallback = std::function<void(const ConnectionInfo&)>;
        using NodeConnectCallback = std::function<void()>;
    public:


        WalletNetworkIO(io::Address address
                      , io::Address node_address
                      , bool is_server
                      , IKeyChain::Ptr keychain
                      , io::Reactor::Ptr reactor = io::Reactor::Ptr()
                      , unsigned reconnect_ms = 1000 // 1 sec
                      , unsigned sync_period_ms = 60 * 1000  // 1 minute
                      , uint64_t start_tag = 0);
        virtual ~WalletNetworkIO();

        void start();
        void stop();

        void add_wallet(const WalletID& walletID, io::Address address);

    private:
        // INetworkIO
        void send_tx_message(const WalletID& to, wallet::Invite&&) override;
        void send_tx_message(const WalletID& to, wallet::ConfirmTransaction&&) override;
        void send_tx_message(const WalletID& to, wallet::ConfirmInvitation&&) override;
        void send_tx_message(const WalletID& to, wallet::TxRegistered&&) override;
        void send_tx_message(const WalletID& to, wallet::TxFailed&&) override;

        void send_node_message(proto::NewTransaction&&) override;
        void send_node_message(proto::GetProofUtxo&&) override;
        void send_node_message(proto::GetHdr&&) override;
        void send_node_message(proto::GetMined&&) override;
        void send_node_message(proto::GetProofState&&) override;

        void close_connection(const WalletID& id) override;
        void connect_node() override;
        void close_node_connection() override;

        // IMsgHandler
        void on_protocol_error(uint64_t fromStream, ProtocolError error) override;;
        void on_connection_error(uint64_t fromStream, io::ErrorCode errorCode) override;

        // handlers for the protocol messages
        bool on_message(uint64_t connectionId, wallet::Invite&& msg);
        bool on_message(uint64_t connectionId, wallet::ConfirmTransaction&& msg);
        bool on_message(uint64_t connectionId, wallet::ConfirmInvitation&& msg);
        bool on_message(uint64_t connectionId, wallet::TxRegistered&& msg);
        bool on_message(uint64_t connectionId, wallet::TxFailed&& msg);
        struct WalletInfo;
        void connect_wallet(const WalletInfo& wallet, uint64_t tag, ConnectCallback&& callback);
        void on_stream_accepted(io::TcpStream::Ptr&& newStream, io::ErrorCode errorCode);
        void on_client_connected(uint64_t tag, io::TcpStream::Ptr&& newStream, io::ErrorCode status);

        void start_sync_timer();
        void on_sync_timer();
        void on_node_connected();

        void close_connection(uint64_t tag);
        uint64_t get_connection_tag();
        void create_node_connection();
        void add_connection(uint64_t tag, ConnectionInfo&&);

        template <typename T>
        void send(const WalletID& walletID, MsgType type, T&& msg)
        {
            update_wallets(walletID);
            if (auto it = m_connectionWalletsIndex.find(walletID, ConnectionWalletIDComparer()); it != m_connectionWalletsIndex.end())
            {
                if (it->m_connection)
                {
                    m_protocol.serialize(m_msgToSend, type, msg);
                    auto res = it->m_connection->write_msg(m_msgToSend);
                    m_msgToSend.clear();
                    test_io_result(res);
                }
            }
            else if (auto it = m_walletsIndex.find(walletID, WalletIDComparer()); it != m_walletsIndex.end())
            {
                auto t = std::make_shared<T>(std::move(msg)); // we need copyable object
                connect_wallet(*it, get_connection_tag(), [this, type, t](const ConnectionInfo& ci)
                {
                    send(ci.m_wallet.m_walletID, type, std::move(*t));
                });
            }
            else
            {
                throw std::runtime_error("failed to send message");
            }
        }

        template<typename T>
        void send_to_node(T&& msg)
        {
            if (!m_is_node_connected)
            {
                auto f = [this, msg = std::move(msg)]()
                {
                    m_node_connection->Send(msg);
                };
                m_node_connect_callbacks.emplace_back(std::move(f));
                connect_node();
            }
            else
            {
                m_node_connection->Send(msg);
            }
        }

        void test_io_result(const io::Result res);
        bool is_connected(uint64_t id);

        const WalletID& get_wallet_id(uint64_t connectionId) const;
        uint64_t get_connection(const WalletID& walletID) const;
        void update_wallets(const WalletID& walletID);

        class WalletNodeConnection : public proto::NodeConnection
        {
        public:
            using NodeConnectCallback = std::function<void()>;
            WalletNodeConnection(const io::Address& address, IWallet& wallet, io::Reactor::Ptr reactor, unsigned reconnectMsec);
            void connect(NodeConnectCallback&& cb);
        private:
            // NodeConnection
            void OnConnected() override;
			void OnDisconnect(const DisconnectReason&) override;
			bool OnMsg2(proto::Boolean&& msg) override;
            bool OnMsg2(proto::ProofUtxo&& msg) override;
			bool OnMsg2(proto::ProofStateForDummies&& msg) override;
			bool OnMsg2(proto::NewTip&& msg) override;
            bool OnMsg2(proto::Hdr&& msg) override;
            bool OnMsg2(proto::Mined&& msg) override;
        private:
            io::Address m_address;
            IWallet & m_wallet;
            std::vector<NodeConnectCallback> m_callbacks;
            bool m_connecting;
            io::Timer::Ptr m_timer;
            unsigned m_reconnectMsec;
        };

    private:

        Protocol m_protocol;
        WalletID m_walletID;
        io::Address m_node_address;
        io::Reactor::Ptr m_reactor;
        io::TcpServer::Ptr m_server;
        IWallet* m_wallet;
        IKeyChain::Ptr m_keychain;


        struct WalletIDTag;
        struct AddressTag;

        using WalletIDHook = bi::set_base_hook <bi::tag<WalletIDTag>>;
        using AddressHook = bi::set_base_hook<bi::tag<AddressTag>>;
        
        struct WalletInfo : public WalletIDHook
                          , public AddressHook
        {
            WalletID m_walletID;
            io::Address m_address;
            WalletInfo(const WalletID& id, io::Address address)
                : m_walletID{id}
                , m_address{address}
            {}
        };

        struct WalletIDComparer
        {
            bool operator()(const WalletInfo& left, const WalletInfo& right) const
            { 
                return left.m_walletID < right.m_walletID; 
            }
            bool operator()(const WalletInfo& left, const WalletID& right) const
            {
                return left.m_walletID < right;
            }
            bool operator()(const WalletID& left, const WalletInfo& right) const
            {
                return left < right.m_walletID;
            }
        };

        struct AddressComparer
        {
            bool operator()(const WalletInfo& left, const WalletInfo& right) const
            { 
                return left.m_address.u64() < right.m_address.u64();
            }

            bool operator()(const WalletInfo& left, const uint64_t& right) const
            {
                return left.m_address.u64() < right;
            }
            bool operator()(const uint64_t& left, const WalletInfo& right) const
            {
                return left < right.m_address.u64();
            }
        };

        struct ConnectionInfo : public WalletIDHook
        {
            uint64_t m_connectionID;
            const WalletInfo& m_wallet;
            ConnectCallback m_callback;
            std::unique_ptr<Connection> m_connection;

            ConnectionInfo(uint64_t id, const WalletInfo& wallet, ConnectCallback&& callback)
                : m_connectionID{ id }
                , m_wallet{ wallet }
                , m_callback{ std::move(callback) }
            {
            }
        };

        struct ConnectionWalletIDComparer
        {
            bool operator()(const ConnectionInfo& left, const ConnectionInfo& right) const
            {
                return left.m_wallet.m_walletID < right.m_wallet.m_walletID;
            }

            bool operator()(const ConnectionInfo& left, const WalletID& right) const
            {
                return left.m_wallet.m_walletID < right;
            }
            bool operator()(const WalletID& left, const ConnectionInfo& right) const
            {
                return left < right.m_wallet.m_walletID;
            }
        };

        std::vector<std::unique_ptr<WalletInfo>> m_wallets;
        std::map<uint64_t, ConnectionInfo> m_connections;
        bi::set<WalletInfo, bi::base_hook<WalletIDHook>, bi::compare<WalletIDComparer>> m_walletsIndex;
        bi::set<WalletInfo, bi::base_hook<AddressHook>, bi::compare<AddressComparer>> m_addressIndex;
        bi::set < ConnectionInfo, bi::base_hook<WalletIDHook>, bi::compare<ConnectionWalletIDComparer>> m_connectionWalletsIndex;

        bool m_is_node_connected;
        uint64_t m_connection_tag;
        io::Reactor::Scope m_reactor_scope;
        unsigned m_reconnect_ms;
        unsigned m_sync_period_ms;
        std::unique_ptr<WalletNodeConnection> m_node_connection;
        SerializedMsg m_msgToSend;
        io::Timer::Ptr m_sync_timer;

        std::vector<NodeConnectCallback> m_node_connect_callbacks;
    };
}
