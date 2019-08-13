// Copyright 2019 The Beam Team
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

#include "client.h"

#include "wallet/bitcoin/bitcoin_core_017.h"
#include "utility/logger.h"
#include "utility/bridge.h"

namespace beam::bitcoin
{
    struct BitcoinClientBridge : public Bridge<IClientAsync>
    {
        BRIDGE_INIT(BitcoinClientBridge);

        void GetStatus()
        {
            call_async(&IClientAsync::GetStatus);
        }

        void GetBalance()
        {
            call_async(&IClientAsync::GetBalance);
        }

        void ResetSettings()
        {
            call_async(&IClientAsync::ResetSettings);
        }
    };
    
    Client::Client(wallet::IWalletDB::Ptr walletDB, io::Reactor& reactor)
        : m_status(Status::Uninitialized)
        , m_reactor(reactor)
        , m_async{ std::make_shared<BitcoinClientBridge>(*(static_cast<IClientAsync*>(this)), reactor) }
        , m_settingsProvider{ std::make_unique<SettingsProvider>(walletDB) }
    {
    }

    IClientAsync::Ptr Client::GetAsync()
    {
        return m_async;
    }

    BitcoinCoreSettings Client::GetBitcoinCoreSettings() const
    {
        Lock lock(m_mutex);
        return m_settingsProvider->GetBitcoinCoreSettings();
    }

    Settings Client::GetSettings() const
    {
        Lock lock(m_mutex);
        return m_settingsProvider->GetSettings();
    }

    void Client::SetSettings(const Settings& settings)
    {
        Lock lock(m_mutex);
        m_settingsProvider->SetSettings(settings);
    }

    void Client::GetStatus()
    {
        OnStatus(m_status);
    }

    void Client::GetBalance()
    {
        if (!m_bridge)
        {
            m_bridge = std::make_shared<BitcoinCore017>(m_reactor, shared_from_this());
        }

        m_bridge->getDetailedBalance([this] (const IBridge::Error& error, double confirmed, double unconfirmed, double immature)
        {
            // TODO: check error and update status
            SetStatus((error.m_type != IBridge::None) ? Status::Failed : Status::Connected);

            Balance balance;
            balance.m_available = confirmed;
            balance.m_unconfirmed = unconfirmed;
            balance.m_immature = immature;

            OnBalance(balance);
        });
    }

    void Client::ResetSettings()
    {
        {
            Lock lock(m_mutex);
            m_settingsProvider->ResetSettings();
        }

        SetStatus(Status::Uninitialized);
    }

    void Client::SetStatus(const Status& status)
    {
        m_status = status;
        OnStatus(m_status);
    }
} // namespace beam::bitcoin