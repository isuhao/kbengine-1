#include "baseapp.hpp"
#include "archiver.hpp"
#include "base.hpp"

namespace KBEngine{	

//-------------------------------------------------------------------------------------
Archiver::Archiver():
	archiveIndex_(INT_MAX),
	backupEntityIDs_()
{
}

//-------------------------------------------------------------------------------------
Archiver::~Archiver()
{
}

//-------------------------------------------------------------------------------------
void Archiver::tick()
{
	int32 periodInTicks = secondsToTicks(ServerConfig::getSingleton().getBaseApp().archivePeriod, 0);
	if (periodInTicks == 0)
		return;

	if (archiveIndex_ >= periodInTicks)
	{
		this->createArchiveTable();
	}

	// 算法如下:
	// base的数量 * idx / tick周期 = 每次在vector中移动的一个区段
	// 这个区段在每个gametick进行处理, 刚好平滑的在periodInTicks中处理完任务
	// 如果archiveIndex_ >= periodInTicks则重新产生一次随机序列
	int size = backupEntityIDs_.size();
	int startIndex = size * archiveIndex_ / periodInTicks;

	++archiveIndex_;

	int endIndex   = size * archiveIndex_ / periodInTicks;

	for (int i = startIndex; i < endIndex; ++i)
	{
		Base * pBase = Baseapp::getSingleton().findEntity(backupEntityIDs_[i]);
		
		if(pBase && pBase->hasDB())
		{
			this->archive(*pBase);
		}
	}
}

//-------------------------------------------------------------------------------------
void Archiver::archive(Base& base)
{
	base.writeToDB(NULL);
}

//-------------------------------------------------------------------------------------
void Archiver::createArchiveTable()
{
	archiveIndex_ = 0;
	backupEntityIDs_.clear();

	Entities<Base>::ENTITYS_MAP::const_iterator iter = Baseapp::getSingleton().pEntities()->getEntities().begin();

	for(; iter != Baseapp::getSingleton().pEntities()->getEntities().end(); iter++)
	{
		if(static_cast<Base*>(iter->second.get())->hasDB())
		{
			backupEntityIDs_.push_back(iter->first);
		}
	}

	// 随机一下序列
	std::random_shuffle(backupEntityIDs_.begin(), backupEntityIDs_.end());
}

}
