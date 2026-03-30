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
#include "shared_protocol.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <array>
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

    _users.clear();

    for (auto& u : doc["users"].GetArray())
    {
        _users[u["username"].GetString()]._password = Decode_64_2_sha(u["password"].GetString());
        _users[u["username"].GetString()]._highscore = u["highscore"].GetUint64();
    }
}

void UserStore::save()
{
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    rapidjson::Value arr(rapidjson::kArrayType);
    for (auto& [user, data] : _users)
    {
        unsigned char encodedBase64[45]{};
        Encode_sha_2_64(data._password, encodedBase64);

        rapidjson::Value obj(rapidjson::kObjectType);
        obj.AddMember("username",
            rapidjson::Value(user.c_str(), alloc), alloc);
        obj.AddMember("password",
            rapidjson::Value(reinterpret_cast<char*>(encodedBase64), alloc), alloc);
        obj.AddMember("highscore", data._highscore, alloc);
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
    return it != _users.end() && it->second._password == pass;
}

bool UserStore::createAccount(const std::string& user, const std::string& pass)
{
    if (_users.contains(user)) return false;
    
    _users[user]._password = pass;
    _users[user]._highscore = 0;
    save();
    return true;
}

void UserStore::saveHighscore(const std::string& user, const std::uint64_t& highscore)
{
    if (_users.contains(user)) return;

    _users[user]._highscore = highscore;
    save();
    load();
    return;
}

std::optional<std::uint64_t> UserStore::getHighscore(const std::string& user) const
{
    auto it = _users.find(user);
    if (it == _users.end()) return std::nullopt;
    else return it->second._highscore;
}
