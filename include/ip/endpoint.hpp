#pragma once

#include <algorithm>
#include <cstring>
#include <sys/socket.h>

namespace ip
{
    class endpoint
    {
    private:
        ::sockaddr_storage d_data{};
        ::socklen_t d_size{sizeof(::sockaddr_storage)};

    public:
        endpoint() = default;
        endpoint(const void *data, ::socklen_t size) : d_size(size)
        {
            std::memcpy(&this->d_data, data, ::std::min(size, ::socklen_t(sizeof(::sockaddr_storage))));
        }
        template <typename ET>
        endpoint(ET &e) : endpoint(e.data(), e.size()) {}

        auto storage() -> ::sockaddr_storage & { return this->d_data; }
        auto storage() const -> const ::sockaddr_storage & { return this->d_data; }
        auto data() -> ::sockaddr * { return reinterpret_cast<::sockaddr *>(&this->d_data); }
        auto data() const -> const ::sockaddr * { return reinterpret_cast<const ::sockaddr *>(&this->d_data); }
        auto size() const -> ::socklen_t { return this->d_size; }
        auto size() -> ::socklen_t & { return this->d_size; }
    };
}