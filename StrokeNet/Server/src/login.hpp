/* Start Header
***********************************************************************/

/*! \file   login.hpp
    \author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#pragma once
#include <string>
#include <unordered_map>

class UserStore
{
public:
    explicit UserStore(const std::string& filepath = "users.json");

    void load();
    void save();
    bool authenticate(const std::string& user, const std::string& pass) const;
    bool createAccount(const std::string& user, const std::string& pass);

private:
    std::string _filepath;
    std::unordered_map<std::string, std::string> _users;
};