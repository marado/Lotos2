#include "config.h"

#include "Creature.h"

#if defined __EXCEPTION_TRACER__
#	include "Exception.h"
#endif


using namespace lotos2;
using lotos2::Creature;


Creature::Creature()
{
	id=0;
}

Creature::~Creature()
{
}

Creature* Creature::getCreature()
{
	return this;
}

const Creature* Creature::getCreature() const
{
	return this;
}

void Creature::setID()
{
	this->id=auto_id|this->idRange();
}

uint32_t Creature::getID() const
{
	return id;
}
