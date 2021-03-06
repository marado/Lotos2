#include "config.h"

#if defined __WINDOWS__ || defined WIN32
#include <winerror.h>
#endif

#include <list>

#include <boost/asio/placeholders.hpp>

#include "server.h"
#include "Scheduler.h"
#include "network/OutputMessage.h"
#include "network/Connection.h"

#include "Logger.h"

#include "globals.h"

using namespace lotos2;
using lotos2::ServiceManager;
using lotos2::ServicePort;


bool ServicePort::m_logError=true;

///////////////////////////////////////////////////////////////////////////////
// Service

ServiceManager::ServiceManager()
	: m_io_service(), death_timer(m_io_service), running(false)
{
}

ServiceManager::~ServiceManager()
{
	stop();
}

std::list<uint16_t> ServiceManager::get_ports() const
{
	std::list<uint16_t> ports;
	for (std::map<uint16_t, ServicePort_ptr>::const_iterator it=m_acceptors.begin(); it!=m_acceptors.end(); ++it) {
		ports.push_back(it->first);
		}
	// Maps are ordered, so the elements are in order
	//ports.sort();
	ports.unique();
	return ports;
}

void ServiceManager::die()
{
	m_io_service.stop();
}

void ServiceManager::run()
{
	assert(!running);
	running=true;
	try {
		m_io_service.run();
		}
	catch (boost::system::system_error& e) {
		LOG_MESSAGE("NETWORK", LOGTYPE_ERROR, e.what());
		}
}

void ServiceManager::stop()
{
	if (!running) {
		return;
		}

	running=false;

	for (std::map<uint16_t, ServicePort_ptr>::iterator it=m_acceptors.begin(); it!=m_acceptors.end(); ++it) {
		try {
			m_io_service.post(boost::bind(&ServicePort::onStopServer, it->second));
			}
		catch (boost::system::system_error& e) {
			LOG_MESSAGE("NETWORK", LOGTYPE_ERROR, e.what());
			}
		}
	m_acceptors.clear();

	network::OutputMessagePool::getInstance()->stop();

	// Give the server 3 seconds to process all messages before death
	death_timer.expires_from_now(boost::posix_time::seconds(3));
	death_timer.async_wait(boost::bind(&ServiceManager::die, this));
}

///////////////////////////////////////////////////////////////////////////////
// ServicePort

ServicePort::ServicePort(boost::asio::io_service& io_service)
	: m_io_service(io_service),
	m_serverPort(0),
	m_pendingStart(false)
{
}

ServicePort::~ServicePort()
{
	close();
}

bool ServicePort::is_single_socket() const
{
	return m_services.size() && m_services.front()->is_single_socket();
}

std::string ServicePort::get_protocol_names() const
{
	if (m_services.empty()) {
		return "";
		}
	std::string str=m_services.front()->get_protocol_name();
	for (uint32_t i=1; i<m_services.size(); ++i) {
		str+=", ";
		str+=m_services[i]->get_protocol_name();
		}
	return str;
}

void ServicePort::accept(Acceptor_ptr acceptor)
{
	try {
		boost::asio::ip::tcp::socket* socket=new boost::asio::ip::tcp::socket(m_io_service);

		acceptor->async_accept(
			*socket,
			boost::bind(
				&ServicePort::onAccept,
				this,
				acceptor,
				socket,
				boost::asio::placeholders::error
				)
			);
		}
	catch (boost::system::system_error& e) {
		if (m_logError) {
			LOG_MESSAGE("NETWORK", LOGTYPE_ERROR, e.what());
			m_logError = false;
            }
        }
}

void ServicePort::onAccept(Acceptor_ptr acceptor, boost::asio::ip::tcp::socket* socket, const boost::system::error_code& error)
{
	if (!error) {
		if (m_services.empty()) {
#ifdef __DEBUG_NET__
			std::cout << "Error: [ServerPort::accept] No services running!" << std::endl;
#endif
			return;
		}

		boost::system::error_code error;
		const boost::asio::ip::tcp::endpoint endpoint=socket->remote_endpoint(error);
		boost::asio::ip::address remote_ip=boost::asio::ip::address();
		if (!error) {
			remote_ip=endpoint.address();
			}

		if (!remote_ip.is_unspecified()) {
			network::Connection_ptr connection=network::ConnectionManager::getInstance()->createConnection(socket, m_io_service, shared_from_this());

			if (m_services.front()->is_single_socket()) {
				// Only one handler, and it will send first
				connection->acceptConnection(m_services.front()->make_protocol(connection));
				}
			else {
				connection->acceptConnection();
				}
			}
		else {
			//close the socket
			if (socket->is_open()) {
				boost::system::error_code error;
				socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
				socket->close(error);
				delete socket;
				}
			}

#ifdef __DEBUG_NET_DETAIL__
		std::cout << "accept - OK" << std::endl;
#endif
		accept(acceptor);
		}
	else {
		if (error!=boost::asio::error::operation_aborted) {
			close();

			if (!m_pendingStart) {
				m_pendingStart=true;
				g_scheduler.addEvent(createSchedulerTask(
						5000,
						boost::bind(
							&ServicePort::openAcceptor,
							boost::weak_ptr<ServicePort>(shared_from_this()),
							m_serverPort
							)
					));
				}
			}
		else {
#ifdef __DEBUG_NET__
			std::cout << "Error: [ServerPort::onAccept] Operation aborted." << std::endl;
#endif
			}
		}
}

network::Protocol* ServicePort::make_protocol(network::NetworkMessage& msg) const
{
	uint8_t protocolId=msg.GetByte();
	for (std::vector<Service_ptr>::const_iterator it=m_services.begin(); it!=m_services.end(); ++it) {
		Service_ptr service=*it;
		if (service->get_protocol_identifier()==protocolId)
			// Correct service! Create protocol and get on with it
			return service->make_protocol(network::Connection_ptr());
		// We can ignore the other cases, they will most likely end up in return NULL anyways.
		}

	return NULL;
}

void ServicePort::onStopServer()
{
	close();
}

void ServicePort::openAcceptor(boost::weak_ptr<ServicePort> weak_service, uint16_t port)
{
	if (weak_service.expired()) {
		return;
		}

	if (ServicePort_ptr service=weak_service.lock()) {
#ifdef __DEBUG_NET_DETAIL__
		std::cout << "ServicePort::openAcceptor" << std::endl;
#endif
		service->open(port);
		}
}

void ServicePort::open(uint16_t port)
{
	m_serverPort=port;
	m_pendingStart=false;

	try {
		// std::cout << "\n" << ip->to_string() << "\n";
		Acceptor_ptr aptr(new boost::asio::ip::tcp::acceptor(m_io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address(), m_serverPort)));

		aptr->set_option(boost::asio::ip::tcp::no_delay(true));
/*
#ifdef USE_IPV6
            Acceptor_ptr aptr6(new boost::asio::ip::tcp::acceptor(m_io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v6(), m_serverPort)));
            aptr6->set_option(boost::asio::ip::tcp::no_delay(true));
            aptr6->set_option(boost::asio::ip::v6_only(true));
#endif
*/

		accept(aptr);
		m_tcp_acceptors.push_back(aptr);
		}
	catch (boost::system::system_error& e) {
		if (m_logError) {
			LOG_MESSAGE("NETWORK", LOGTYPE_ERROR, e.what());
			m_logError=false;
			}

		m_pendingStart=true;
		g_scheduler.addEvent(createSchedulerTask(
				5000,
				boost::bind(
					&ServicePort::openAcceptor,
					boost::weak_ptr<ServicePort>(shared_from_this()),
					port
					)
				));
		}
}

void ServicePort::close()
{
	for (std::vector<Acceptor_ptr>::iterator aptr=m_tcp_acceptors.begin(); aptr!=m_tcp_acceptors.end(); ++aptr) {
		if ((*aptr)->is_open()) {
			boost::system::error_code error;
			(*aptr)->close(error);
			if (error) {
				PRINT_ASIO_ERROR("Closing listen socket");
				}
			}
		}
	m_tcp_acceptors.clear();
}

bool ServicePort::add_service(Service_ptr new_svc)
{
	for (std::vector<Service_ptr>::const_iterator svc_iter=m_services.begin(); svc_iter!=m_services.end(); ++svc_iter) {
		Service_ptr svc=*svc_iter;
		if (svc->is_single_socket()) {
			return false;
			}
		}

	m_services.push_back(new_svc);
	return true;
}
