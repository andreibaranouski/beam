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

#include "wallet/common_utils.h"

#include <boost/format.hpp>
#include "wallet/strings_resources.h"
#include "utility/logger.h"

namespace beam::wallet
{
WalletAddress GenerateNewAddress(
        const IWalletDB::Ptr& walletDB,
        const std::string& label,
        IPrivateKeyKeeper::Ptr keyKeeper,
        WalletAddress::ExpirationStatus expirationStatus,
        bool saveRequired)
{
    WalletAddress address = storage::createAddress(*walletDB, keyKeeper);

    address.setExpiration(expirationStatus);
    address.m_label = label;
    if (saveRequired)
    {
        walletDB->saveAddress(address);
    }

    LOG_INFO() << boost::format(kAddrNewGenerated) 
               % std::to_string(address.m_walletID);
    if (!label.empty()) {
        LOG_INFO() << boost::format(kAddrNewGeneratedLabel) % label;
    }
    return address;
}
}  // namespace beam::wallet
