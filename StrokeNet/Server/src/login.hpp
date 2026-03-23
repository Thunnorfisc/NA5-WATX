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