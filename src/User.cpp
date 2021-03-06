#include "config.h"

#include "User.h"
#include "Scheduler.h"
#include "Talker.h"

#if defined __EXCEPTION_TRACER__
#	include "Exception.h"
#endif


using namespace lotos2;
using lotos2::User;


boost::recursive_mutex AutoID::autoIDLock;
uint32_t AutoID::count=1000;
AutoID::list_type AutoID::list;

AutoList<User> User::listUser;

#ifdef __ENABLE_SERVER_DIAGNOSTIC__
uint32_t User::userCount=0;
#endif


User::User(const std::string& _name, network::ProtocolTelnet* p)
	: Creature()
{
	client=p;
	if (client) {
		client->setUser(this);
		}
	name=_name;

#ifdef __ENABLE_SERVER_DIAGNOSTIC__
	userCount++;
#endif
}

User::~User()
{
#ifdef __ENABLE_SERVER_DIAGNOSTIC__
	userCount--;
#endif
}

void User::addList()
{
	listUser.addList(this);
}

void User::removeList()
{
	listUser.removeList(getID());
}

boost::asio::ip::address User::getIP() const
{
	if (client) {
		return client->getIP();
		}
	return boost::asio::ip::address();
}

void User::parseLine(const std::string& line)
{
	uWrite(line+"\n");
	prompt();
}

void User::prompt()
{
	uWrite(">");
}

template<typename _CharT>
void User::uWrite(const _CharT message)
{
    network::OutputMessage_ptr output=network::OutputMessagePool::getInstance()->getOutputMessage(client, false);
    output->AddString(message);
    network::OutputMessagePool::getInstance()->send(output);
}
