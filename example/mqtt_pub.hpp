#include <iostream>

#include <async_mqtt/protocol/connection.hpp>
#include <async_mqtt/util/setup_log.hpp>

namespace am = async_mqtt;

template <typename T, typename = void>
struct has_socket_api : std::false_type
{
};

template <typename T>
struct has_socket_api<T, std::void_t<
                             decltype(std::declval<T>().Send(
                                 std::declval<const void *>(),
                                 std::declval<size_t>(),
                                 std::declval<int>(),
                                 std::declval<int>())),
                             decltype(std::declval<T>().Recv(
                                 std::declval<void *>(),
                                 std::declval<size_t>(),
                                 std::declval<int>(),
                                 std::declval<int>())),
                             decltype(std::declval<T>().Close())>> : std::true_type
{
};

template <typename T>
inline constexpr bool has_socket_api_v = has_socket_api<T>::value;

template <typename SocketType>
class mqtt_connection : public am::connection<am::role::client>
{
    static_assert(
        has_socket_api_v<SocketType>,
        "SocketType must provide:\n"
        "  ssize_t Recv(void*, size_t, int, int);\n"
        "  ssize_t Send(const void*, size_t, int, int);\n"
        "  int Close();");

public:
    mqtt_connection(SocketType socket)
        : am::connection<am::role::client>{am::protocol_version::v5},
          socket_{std::move(socket)}
    {
        set_auto_pub_response(true);
    }

    SocketType &socket()
    {
        return socket_;
    }

private:
    void on_error(am::error_code ec) override final
    {
        std::cout << "on_error: " << ec.message() << std::endl;
        socket_.Close();
        notify_closed();
    }

    void on_close() override final
    {
        std::cout << "on_close" << std::endl;
        socket_.Close();
        notify_closed();
    }

    void on_send(
        am::packet_variant packet,
        std::optional<am::packet_id_type>
            release_packet_id_if_send_error = std::nullopt) override final
    {
        std::cout << "send:" << packet << std::endl;
        try
        {
            auto buffers = packet.const_buffer_sequence();

            std::size_t total_size = 0;
            for (const auto &buf : buffers)
                total_size += buf.size();
            std::vector<char> flat_buffer(total_size);

            char *dest = flat_buffer.data();
            for (const auto &buf : buffers)
            {
                std::memcpy(dest, buf.data(), buf.size());
                dest += buf.size();
            }

            ssize_t sent = socket_.Send(
                flat_buffer.data(),
                flat_buffer.size(),
                0,
                -1);

            if (sent < 0 || static_cast<std::size_t>(sent) != total_size)
            {
                if (release_packet_id_if_send_error)
                {
                    release_packet_id(*release_packet_id_if_send_error);
                }
                throw am::system_error(
                    am::error_code(errno, am::generic_category()),
                    "send failed");
            }
        }
        catch (am::system_error const &se)
        {
            if (release_packet_id_if_send_error)
            {
                release_packet_id(*release_packet_id_if_send_error);
            }
            throw;
        }
    }

    void on_packet_id_release(
        am::packet_id_type /*packet_id*/
        ) override final
    {
    }

    void on_receive(am::packet_variant packet) override final
    {
        std::cout << "on_receive: " << packet << std::endl;
        packet.visit(
            am::overload{
                [&](am::v5::connack_packet const &p)
                {
                    if (make_error_code(p.code()))
                    {
                        std::cout << p.code() << std::endl;
                        socket_.Close();
                        notify_closed();
                    }
                    else
                    {
                        // publish
                        send(
                            am::v5::publish_packet{
                                "topic1",
                                "payload1",
                                am::qos::at_most_once});
                        send(
                            am::v5::publish_packet{
                                *acquire_unique_packet_id(),
                                "topic2",
                                "payload2",
                                am::qos::at_least_once});
                        send(
                            am::v5::publish_packet{
                                *acquire_unique_packet_id(),
                                "topic3",
                                "payload3",
                                am::qos::exactly_once});
                    }
                },
                [&](am::v5::publish_packet const &p)
                {
                    std::cout
                        << "MQTT PUBLISH recv"
                        << " pid:" << p.packet_id()
                        << " topic:" << p.topic()
                        << " payload:" << p.payload()
                        << " qos:" << p.opts().get_qos()
                        << " retain:" << p.opts().get_retain()
                        << " dup:" << p.opts().get_dup()
                        << std::endl;
                },
                [&](am::v5::puback_packet const &p)
                {
                    std::cout
                        << "MQTT PUBACK recv"
                        << " pid:" << p.packet_id()
                        << std::endl;
                },
                [&](am::v5::pubrec_packet const &p)
                {
                    std::cout
                        << "MQTT PUBREC recv"
                        << " pid:" << p.packet_id()
                        << std::endl;
                },
                [&](am::v5::pubrel_packet const &p)
                {
                    std::cout
                        << "MQTT PUBREL recv"
                        << " pid:" << p.packet_id()
                        << std::endl;
                },
                [&](am::v5::pubcomp_packet const &p)
                {
                    std::cout
                        << "MQTT PUBCOMP recv"
                        << " pid:" << p.packet_id()
                        << std::endl;
                    send(
                        am::v5::disconnect_packet{});
                },
                [](auto const &) {}});
    }

    void on_timer_op(
        am::timer_op op,
        am::timer_kind kind,
        std::optional<std::chrono::milliseconds> ms) override final
    {
        std::cout
            << "timer"
            << " op:" << op
            << " kind:" << kind;
        if (ms)
        {
            std::cout << " ms:" << ms->count();
        }
        std::cout << std::endl;
    }

private:
    SocketType socket_;
};
