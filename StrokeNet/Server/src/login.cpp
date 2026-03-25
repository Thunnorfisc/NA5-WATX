/* Start Header
***********************************************************************/

/*! \file   login.cpp
    \author William Wibisana Dumanauw
    \par    email: williamwibisana.d@digipen.edu
    \date   20th March, 2026
    \brief  Copyright (C) 2026 DigiPen Institute of Technology

    Reproduction or diclosure of this file or its contents without the prior
    written consent of DigiPen Institute of Technology is prohibited. */

/* End Header
***********************************************************************/
#include "login.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <fstream>
#include <sstream>

UserStore::UserStore(const std::string& filepath) : _filepath(filepath)
{}

void UserStore::load()
{
    std::ifstream ifs(_filepath);
    if (!ifs.is_open()) return;

    std::ostringstream ss;
    ss << ifs.rdbuf();

    rapidjson::Document doc;
    doc.Parse(ss.str().c_str());

    if (!doc.IsObject() || !doc.HasMember("users")) return;

    for (auto& u : doc["users"].GetArray())
    {
        _users[u["username"].GetString()] = u["password"].GetString();
    }
}

void UserStore::save()
{
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    rapidjson::Value arr(rapidjson::kArrayType);
    for (auto& [user, pass] : _users)
    {
        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("username",
            rapidjson::Value(user.c_str(), alloc), alloc);
        obj.AddMember("password",
            rapidjson::Value(pass.c_str(), alloc), alloc);
        obj.AddMember("score", rapidjson::Value(0, alloc), alloc);
        arr.PushBack(obj, alloc);
    }
    doc.AddMember("users", arr, alloc);

    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(sb);
    doc.Accept(writer);

    std::ofstream ofs(_filepath);
    ofs << sb.GetString();
}

bool UserStore::authenticate(const std::string& user, const std::string& pass) const
{
    auto it = _users.find(user);
    return it != _users.end() && it->second == pass;
}

bool UserStore::createAccount(const std::string& user, const std::string& pass)
{
    if (_users.contains(user)) return false;
    _users[user] = pass;
    save();
    return true;
}
