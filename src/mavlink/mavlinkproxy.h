#ifndef AUTOFLIGHT_MAVLINKPROXY_H
#define AUTOFLIGHT_MAVLINKPROXY_H

#define MAVLINK_PORT 14550

#define MAVLINK_HEARTBEAT_INTERVAL 1000
#define MAVLINK_NAVDATA_INTERVAL 25

#include <interface/inavdatalistener.h>
#include <interface/iconnectionstatuslistener.h>
#include <drones/bebop/types.h>

#include <memory>

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <mavlink/common/mavlink.h>

class MAVLinkProxy : public INavdataListener, public IConnectionStatusListener
{
    public:
        MAVLinkProxy();

        void start();
        void stop();

        void navdataAvailable(std::shared_ptr<const drone::navdata> nd);
        void connectionEstablished();
        void connectionLost();

    private:
        void heartbeat();
        void navdata();

        void dataReceived(const boost::system::error_code &error, size_t received_bytes);

        bool _worker_running = false;

        boost::asio::io_service _io_service;
        std::unique_ptr<boost::thread> _worker;

        boost::asio::ip::udp::socket _socket;
        boost::asio::ip::udp::endpoint _endpoint;

        boost::asio::deadline_timer _heartbeat_timer;
        boost::asio::deadline_timer _navdata_timer;

        bool _flying = false;
        bool _connected = false;
        mavlink_system_t _mavlink_system;
        mavlink_sys_status_t _sys_status;
        mavlink_attitude_t _attitude;
        mavlink_gps_raw_int_t _gps;

        uint8_t _received_msg_buf[MAVLINK_MAX_PACKET_LEN];
};

#endif
