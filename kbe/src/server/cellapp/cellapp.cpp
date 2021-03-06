/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2012 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "cellapp.hpp"
#include "space.hpp"
#include "cellapp_interface.hpp"
#include "forward_message_over_handler.hpp"
#include "network/tcp_packet.hpp"
#include "network/udp_packet.hpp"
#include "server/componentbridge.hpp"
#include "server/components.hpp"
#include "dbmgr/dbmgr_interface.hpp"

#include "../../server/baseappmgr/baseappmgr_interface.hpp"
#include "../../server/cellappmgr/cellappmgr_interface.hpp"
#include "../../server/baseapp/baseapp_interface.hpp"
#include "../../server/cellapp/cellapp_interface.hpp"
#include "../../server/dbmgr/dbmgr_interface.hpp"
#include "../../server/loginapp/loginapp_interface.hpp"

namespace KBEngine{
	
ServerConfig g_serverConfig;
KBE_SINGLETON_INIT(Cellapp);

//-------------------------------------------------------------------------------------
Cellapp::Cellapp(Mercury::EventDispatcher& dispatcher, 
			 Mercury::NetworkInterface& ninterface, 
			 COMPONENT_TYPE componentType,
			 COMPONENT_ID componentID):
	EntityApp<Entity>(dispatcher, ninterface, componentType, componentID),
	pCellAppData_(NULL),
	forward_messagebuffer_(ninterface)
{
	KBEngine::Mercury::MessageHandlers::pMainMessageHandlers = &CellappInterface::messageHandlers;
}

//-------------------------------------------------------------------------------------
Cellapp::~Cellapp()
{
}

//-------------------------------------------------------------------------------------
bool Cellapp::installPyModules()
{
	Entity::installScript(getScript().getModule());
	GlobalDataClient::installScript(getScript().getModule());

	registerScript(Entity::getScriptType());
	
	// 注册创建entity的方法到py
	APPEND_SCRIPT_MODULE_METHOD(getScript().getModule(),		time,							__py_gametime,						METH_VARARGS,			0);
	APPEND_SCRIPT_MODULE_METHOD(getScript().getModule(),		createEntity,					__py_createEntity,					METH_VARARGS,			0);
	APPEND_SCRIPT_MODULE_METHOD(getScript().getModule(), 		executeRawDatabaseCommand,		__py_executeRawDatabaseCommand,		METH_VARARGS,			0);
	return EntityApp<Entity>::installPyModules();
}

//-------------------------------------------------------------------------------------
void Cellapp::onInstallPyModules()
{
	// 添加globalData, cellAppData支持
	pCellAppData_ = new GlobalDataClient(DBMGR_TYPE, GlobalDataServer::CELLAPP_DATA);
	registerPyObjectToScript("cellAppData", pCellAppData_);
}

//-------------------------------------------------------------------------------------
bool Cellapp::uninstallPyModules()
{	
	unregisterPyObjectToScript("cellAppData");
	S_RELEASE(pCellAppData_); 

	Entity::uninstallScript();
	return EntityApp<Entity>::uninstallPyModules();
}

//-------------------------------------------------------------------------------------
PyObject* Cellapp::__py_gametime(PyObject* self, PyObject* args)
{
	return PyLong_FromUnsignedLong(Cellapp::getSingleton().time());
}

//-------------------------------------------------------------------------------------
bool Cellapp::run()
{
	return EntityApp<Entity>::run();
}

//-------------------------------------------------------------------------------------
void Cellapp::handleTimeout(TimerHandle handle, void * arg)
{
	switch (reinterpret_cast<uintptr>(arg))
	{
		case TIMEOUT_LOADING_TICK:
			break;
		default:
			break;
	}

	EntityApp<Entity>::handleTimeout(handle, arg);
}

//-------------------------------------------------------------------------------------
void Cellapp::handleGameTick()
{
	EntityApp<Entity>::handleGameTick();
}

//-------------------------------------------------------------------------------------
bool Cellapp::initializeBegin()
{
	return true;
}

//-------------------------------------------------------------------------------------
bool Cellapp::initializeEnd()
{
	mainDispatcher_.clearSpareTime();
	return true;
}

//-------------------------------------------------------------------------------------
void Cellapp::finalise()
{
	EntityApp<Entity>::finalise();
}

//-------------------------------------------------------------------------------------
void Cellapp::onGetEntityAppFromDbmgr(Mercury::Channel* pChannel, int32 uid, std::string& username, 
						int8 componentType, uint64 componentID, 
						uint32 intaddr, uint16 intport, uint32 extaddr, uint16 extport)
{
	EntityApp<Entity>::onRegisterNewApp(pChannel, uid, username, componentType, componentID, 
									intaddr, intport, extaddr, extport);

	KBEngine::COMPONENT_TYPE tcomponentType = (KBEngine::COMPONENT_TYPE)componentType;

	Components::COMPONENTS cts = Componentbridge::getComponents().getComponents(DBMGR_TYPE);
	KBE_ASSERT(cts.size() >= 1);
	
	Components::ComponentInfos* cinfos = Componentbridge::getComponents().findComponent(tcomponentType, uid, componentID);
	cinfos->pChannel = NULL;

	int ret = Components::getSingleton().connectComponent(tcomponentType, uid, componentID);
	KBE_ASSERT(ret != -1);

	Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();

	switch(tcomponentType)
	{
	case BASEAPP_TYPE:
		(*pBundle).newMessage(BaseappInterface::onRegisterNewApp);
		BaseappInterface::onRegisterNewAppArgs8::staticAddToBundle((*pBundle), getUserUID(), getUsername(), CELLAPP_TYPE, componentID_, 
			this->getNetworkInterface().intaddr().ip, this->getNetworkInterface().intaddr().port, 
			this->getNetworkInterface().extaddr().ip, this->getNetworkInterface().extaddr().port);
		break;
	case CELLAPP_TYPE:
		(*pBundle).newMessage(CellappInterface::onRegisterNewApp);
		CellappInterface::onRegisterNewAppArgs8::staticAddToBundle((*pBundle), getUserUID(), getUsername(), CELLAPP_TYPE, componentID_, 
			this->getNetworkInterface().intaddr().ip, this->getNetworkInterface().intaddr().port, 
			this->getNetworkInterface().extaddr().ip, this->getNetworkInterface().extaddr().port);
		break;
	default:
		KBE_ASSERT(false && "no support!\n");
		break;
	};
	
	(*pBundle).send(this->getNetworkInterface(), cinfos->pChannel);
	Mercury::Bundle::ObjPool().reclaimObject(pBundle);
}

//-------------------------------------------------------------------------------------
PyObject* Cellapp::__py_createEntity(PyObject* self, PyObject* args)
{
	PyObject* params = NULL;
	char* entityType = NULL;
	SPACE_ID spaceID;
	PyObject* position, *direction;
	
	if(!PyArg_ParseTuple(args, "s|I|O|O|O", &entityType, &spaceID, &position, &direction, &params))
	{
		PyErr_Format(PyExc_TypeError, 
			"KBEngine::createEntity: args is error! args[scriptName, spaceID, position, direction, states].");
		PyErr_PrintEx(0);
		S_Return;
	}
	
	if(entityType == NULL || strlen(entityType) == 0)
	{
		PyErr_Format(PyExc_TypeError, "KBEngine::createEntity: entityType is NULL.");
		PyErr_PrintEx(0);
		S_Return;
	}

	Space* space = Spaces::findSpace(spaceID);
	if(space == NULL)
	{
		PyErr_Format(PyExc_TypeError, "KBEngine::createEntity: spaceID %ld not found.", spaceID);
		PyErr_PrintEx(0);
		S_Return;
	}
	
	// 创建entity
	Entity* pEntity = Cellapp::getSingleton().createEntityCommon(entityType, params, false, 0);

	if(pEntity != NULL)
	{
		Py_INCREF(pEntity);
		pEntity->initializeEntity(params);
		pEntity->pySetPosition(position);
		pEntity->pySetDirection(direction);	
		
		// 添加到space
		space->addEntity(pEntity);
	}
	
	//Py_XDECREF(params);
	return pEntity;
}

//-------------------------------------------------------------------------------------
PyObject* Cellapp::__py_executeRawDatabaseCommand(PyObject* self, PyObject* args)
{
	int argCount = PyTuple_Size(args);
	PyObject* pycallback = NULL;
	int ret = -1;

	char* data = NULL;
	Py_ssize_t size;
	
	if(argCount == 2)
		ret = PyArg_ParseTuple(args, "s#|O", &data, &size, &pycallback);
	else if(argCount == 1)
		ret = PyArg_ParseTuple(args, "s#", &data, &size);

	if(ret == -1)
	{
		PyErr_Format(PyExc_TypeError, "KBEngine::executeRawDatabaseCommand: args is error!");
		PyErr_PrintEx(0);
	}
	
	Cellapp::getSingleton().executeRawDatabaseCommand(data, size, pycallback);
	S_Return;
}

//-------------------------------------------------------------------------------------
void Cellapp::executeRawDatabaseCommand(const char* datas, uint32 size, PyObject* pycallback)
{
	if(datas == NULL)
	{
		ERROR_MSG("Cellapp::executeRawDatabaseCommand: execute is error!\n");
		return;
	}

	Components::COMPONENTS cts = Components::getSingleton().getComponents(DBMGR_TYPE);
	Components::ComponentInfos* dbmgrinfos = NULL;

	if(cts.size() > 0)
		dbmgrinfos = &(*cts.begin());

	if(dbmgrinfos == NULL || dbmgrinfos->pChannel == NULL || dbmgrinfos->cid == 0)
	{
		ERROR_MSG("Cellapp::executeRawDatabaseCommand: not found dbmgr!\n");
		return;
	}

	DEBUG_MSG("KBEngine::executeRawDatabaseCommand:%s.\n", datas);

	Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
	(*pBundle).newMessage(DbmgrInterface::executeRawDatabaseCommand);
	(*pBundle) << componentID_ << componentType_;

	CALLBACK_ID callbackID = 0;

	if(pycallback && PyCallable_Check(pycallback))
		callbackID = callbackMgr().save(pycallback);

	(*pBundle) << callbackID;
	(*pBundle) << size;
	(*pBundle).append(datas, size);
	(*pBundle).send(this->getNetworkInterface(), dbmgrinfos->pChannel);
	Mercury::Bundle::ObjPool().reclaimObject(pBundle);
}

//-------------------------------------------------------------------------------------
void Cellapp::onExecuteRawDatabaseCommandCB(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	std::string err;
	CALLBACK_ID callbackID = 0;
	uint32 nrows = 0;
	uint32 nfields = 0;
	uint64 affectedRows = 0;

	PyObject* pResultSet = NULL;
	PyObject* pAffectedRows = NULL;
	PyObject* pErrorMsg = NULL;

	s >> callbackID;
	s >> err;

	if(err.size() <= 0)
	{
		s >> nfields;

		pErrorMsg = Py_None;
		Py_INCREF(pErrorMsg);

		if(nfields > 0)
		{
			pAffectedRows = Py_None;
			Py_INCREF(pAffectedRows);

			s >> nrows;

			pResultSet = PyList_New(nrows);
			for (uint32 i = 0; i < nrows; ++i)
			{
				PyObject* pRow = PyList_New(nfields);
				for(uint32 j = 0; j < nfields; ++j)
				{
					std::string cell;
					s.readBlob(cell);

					PyObject* pCell = NULL;
						
					if(cell == "NULL")
					{
						Py_INCREF(Py_None);
						pCell = Py_None;
					}
					else
					{
						pCell = PyBytes_FromStringAndSize(cell.data(), cell.length());
					}

					PyList_SET_ITEM(pRow, j, pCell);
				}

				PyList_SET_ITEM(pResultSet, i, pRow);
			}
		}
		else
		{
			pResultSet = Py_None;
			Py_INCREF(pResultSet);

			pErrorMsg = Py_None;
			Py_INCREF(pErrorMsg);

			s >> affectedRows;

			pAffectedRows = PyLong_FromUnsignedLongLong(affectedRows);
		}
	}
	else
	{
			pResultSet = Py_None;
			Py_INCREF(pResultSet);

			pErrorMsg = PyUnicode_FromString(err.c_str());

			pAffectedRows = Py_None;
			Py_INCREF(pAffectedRows);
	}

	DEBUG_MSG("Cellapp::onExecuteRawDatabaseCommandCB: nrows=%u, nfields=%u, err=%s.\n", nrows, nfields, err.c_str());

	if(callbackID > 0)
	{
		PyObjectPtr pyfunc = pyCallbackMgr_.take(callbackID);
		PyObject* pyResult = PyObject_CallFunction(pyfunc.get(), 
											const_cast<char*>("OOO"), 
											pResultSet, pAffectedRows, pErrorMsg);

		if(pyResult != NULL)
			Py_DECREF(pyResult);
		else
			SCRIPT_ERROR_CHECK();
	}

	Py_XDECREF(pResultSet);
	Py_XDECREF(pAffectedRows);
	Py_XDECREF(pErrorMsg);
}

//-------------------------------------------------------------------------------------
void Cellapp::reqBackupEntityCellData(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	ENTITY_ID entityID = 0;
	s >> entityID;

	Entity* e = this->findEntity(entityID);
	if(!e)
	{
		ERROR_MSG("Cellapp::reqBackupEntityCellData: not found entity %d.\n", entityID);
		return;
	}

	e->backupCellData();
}

//-------------------------------------------------------------------------------------
void Cellapp::reqWriteToDBFromBaseapp(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	ENTITY_ID entityID = 0;
	CALLBACK_ID callbackID = 0;

	s >> entityID;
	s >> callbackID;

	Entity* e = this->findEntity(entityID);
	if(!e)
	{
		ERROR_MSG("Cellapp::reqWriteToDBFromBaseapp: not found entity %d.\n", entityID);
		return;
	}

	e->writeToDB(&callbackID);
}

//-------------------------------------------------------------------------------------
void Cellapp::onDbmgrInitCompleted(Mercury::Channel* pChannel, 
		ENTITY_ID startID, ENTITY_ID endID, int32 startGlobalOrder, int32 startGroupOrder)
{
	EntityApp<Entity>::onDbmgrInitCompleted(pChannel, startID, endID, startGlobalOrder, startGroupOrder);
	
	// 所有脚本都加载完毕
	PyObject* pyResult = PyObject_CallMethod(getEntryScript().get(), 
										const_cast<char*>("onInit"), 
										const_cast<char*>("i"), 
										0);

	if(pyResult != NULL)
		Py_DECREF(pyResult);
	else
		SCRIPT_ERROR_CHECK();
}

//-------------------------------------------------------------------------------------
void Cellapp::onBroadcastCellAppDataChange(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{

	std::string key, value;
	bool isDelete;
	
	s >> isDelete;
	s.readBlob(key);

	if(!isDelete)
	{
		s.readBlob(value);
	}

	PyObject * pyKey = script::Pickler::unpickle(key);
	if(pyKey == NULL)
	{
		ERROR_MSG("Cellapp::onBroadcastCellAppDataChange: no has key!\n");
		return;
	}

	Py_INCREF(pyKey);

	if(isDelete)
	{
		if(pCellAppData_->del(pyKey))
		{
			// 通知脚本
			SCRIPT_OBJECT_CALL_ARGS1(getEntryScript().get(), const_cast<char*>("onCellAppDataDel"), 
				const_cast<char*>("O"), pyKey);
		}
	}
	else
	{
		PyObject * pyValue = script::Pickler::unpickle(value);

		if(pyValue == NULL)
		{
			ERROR_MSG("Cellapp::onBroadcastCellAppDataChange: no has value!\n");
			Py_DECREF(pyKey);
			return;
		}

		Py_INCREF(pyValue);

		if(pCellAppData_->write(pyKey, pyValue))
		{
			// 通知脚本
			SCRIPT_OBJECT_CALL_ARGS2(getEntryScript().get(), const_cast<char*>("onCellAppData"), 
				const_cast<char*>("OO"), pyKey, pyValue);
		}

		Py_DECREF(pyValue);
	}

	Py_DECREF(pyKey);
}

//-------------------------------------------------------------------------------------
void Cellapp::onCreateInNewSpaceFromBaseapp(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	std::string entityType;
	ENTITY_ID mailboxEntityID;
	std::string strEntityCellData;
	COMPONENT_ID componentID;
	SPACE_ID spaceID = 1;

	s >> entityType;
	s >> mailboxEntityID;
	s >> spaceID;
	s >> componentID;

	s.readBlob(strEntityCellData);

	// DEBUG_MSG("Cellapp::onCreateInNewSpaceFromBaseapp: spaceID=%u, entityType=%s, entityID=%d, componentID=%"PRAppID".\n", 
	//	spaceID, entityType.c_str(), mailboxEntityID, componentID);

	Space* space = Spaces::createNewSpace(spaceID);
	if(space != NULL)
	{
		// 解包cellData信息.
		PyObject* params = NULL;
		if(strEntityCellData.size() > 0)
			params = script::Pickler::unpickle(strEntityCellData);
	
		// 创建entity
		Entity* e = createEntityCommon(entityType.c_str(), params, false, mailboxEntityID);
		
		if(e == NULL)
		{
			Py_XDECREF(params);
			return;
		}

		// 设置entity的baseMailbox
		EntityMailbox* mailbox = new EntityMailbox(e->getScriptModule(), NULL, componentID, mailboxEntityID, MAILBOX_TYPE_BASE);
		e->setBaseMailbox(mailbox);
		
		// 此处baseapp可能还有没初始化过来， 所以有一定概率是为None的
		Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(BASEAPP_TYPE, componentID);
		if(cinfos == NULL || cinfos->pChannel == NULL)
		{
			Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
			ForwardItem* pFI = new ForwardItem();
			pFI->pHandler = new FMH_Baseapp_onEntityGetCellFrom_onCreateInNewSpaceFromBaseapp(e, spaceID, params);
			//Py_XDECREF(params);
			pFI->pBundle = pBundle;
			(*pBundle).newMessage(BaseappInterface::onEntityGetCell);
			BaseappInterface::onEntityGetCellArgs3::staticAddToBundle((*pBundle), mailboxEntityID, componentID_, spaceID);
			forward_messagebuffer_.push(componentID, pFI);
			WARNING_MSG("Cellapp::onCreateInNewSpaceFromBaseapp: not found baseapp, message is buffered.\n");
			return;
		}

		// 添加到space
		space->creatorID(e->getID());
		space->addEntity(e);
		
		e->initializeEntity(params);
		Py_XDECREF(params);

		Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
		(*pBundle).newMessage(BaseappInterface::onEntityGetCell);
		BaseappInterface::onEntityGetCellArgs3::staticAddToBundle((*pBundle), mailboxEntityID, componentID_, spaceID);
		(*pBundle).send(this->getNetworkInterface(), cinfos->pChannel);
		Mercury::Bundle::ObjPool().reclaimObject(pBundle);
		return;
	}
	
	ERROR_MSG("Cellapp::onCreateInNewSpaceFromBaseapp: not found baseapp[%"PRAppID"], entityID=%d, spaceID=%u.\n", 
		componentID, mailboxEntityID, spaceID);
}

//-------------------------------------------------------------------------------------
void Cellapp::onCreateCellEntityFromBaseapp(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	std::string entityType;
	ENTITY_ID createToEntityID, entityID;
	std::string strEntityCellData;
	COMPONENT_ID componentID;
	SPACE_ID spaceID = 1;
	bool hasClient;

	s >> createToEntityID;
	s >> entityType;
	s >> entityID;
	s >> componentID;
	s >> hasClient;

	s.readBlob(strEntityCellData);

	// 此处baseapp可能还有没初始化过来， 所以有一定概率是为None的
	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(BASEAPP_TYPE, componentID);
	if(cinfos == NULL || cinfos->pChannel == NULL)
	{
		Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
		ForwardItem* pFI = new ForwardItem();
		pFI->pHandler = new FMH_Baseapp_onEntityGetCellFrom_onCreateCellEntityFromBaseapp(entityType, createToEntityID, 
			entityID, (ArraySize)strEntityCellData.size(), strEntityCellData, hasClient, componentID, spaceID);

		pFI->pBundle = pBundle;
		(*pBundle).newMessage(BaseappInterface::onEntityGetCell);
		BaseappInterface::onEntityGetCellArgs3::staticAddToBundle((*pBundle), entityID, componentID_, spaceID);
		forward_messagebuffer_.push(componentID, pFI);
		WARNING_MSG("Cellapp::onCreateCellEntityFromBaseapp: not found baseapp, message is buffered.\n");
		return;
	}

	_onCreateCellEntityFromBaseapp(entityType, createToEntityID, entityID, (ArraySize)strEntityCellData.size(), 
		strEntityCellData, hasClient, componentID, spaceID);
}

//-------------------------------------------------------------------------------------
void Cellapp::_onCreateCellEntityFromBaseapp(std::string& entityType, ENTITY_ID createToEntityID, 
											 ENTITY_ID entityID, ArraySize cellDataLength, 
											std::string& strEntityCellData, bool hasClient, 
											COMPONENT_ID componentID, SPACE_ID spaceID)
{
	// 注意：此处理论不会找不到组件， 因为onCreateCellEntityFromBaseapp中已经进行过一次消息缓存判断
	Components::ComponentInfos* cinfos = Components::getSingleton().findComponent(BASEAPP_TYPE, componentID);
	KBE_ASSERT(cinfos != NULL && cinfos->pChannel != NULL);

	Entity* pCreateToEntity = pEntities_->find(createToEntityID);

	// 可能spaceEntity已经销毁了， 但还未来得及通知到baseapp时
	// base部分在向这个space创建entity
	if(pCreateToEntity == NULL)
	{
		ERROR_MSG("Cellapp::_onCreateCellEntityFromBaseapp: not fount spaceEntity. may have been destroyed!\n");

		Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
		pBundle->newMessage(BaseappInterface::onCreateCellFailure);
		BaseappInterface::onCreateCellFailureArgs1::staticAddToBundle(*pBundle, entityID);
		pBundle->send(this->getNetworkInterface(), cinfos->pChannel);
		Mercury::Bundle::ObjPool().reclaimObject(pBundle);
		return;
	}

	spaceID = pCreateToEntity->getSpaceID();

	//DEBUG_MSG("Cellapp::onCreateCellEntityFromBaseapp: spaceID=%u, entityType=%s, entityID=%d, componentID=%"PRAppID".\n", 
	//	spaceID, entityType.c_str(), entityID, componentID);

	Space* space = Spaces::findSpace(spaceID);
	if(space != NULL)
	{
		// 解包cellData信息.
		PyObject* params = NULL;
		if(strEntityCellData.size() > 0)
			params = script::Pickler::unpickle(strEntityCellData);
	
		// 创建entity
		Entity* e = createEntityCommon(entityType.c_str(), params, false, entityID);
		
		if(e == NULL)
		{
			Py_XDECREF(params);
			return;
		}

		// 设置entity的baseMailbox
		EntityMailbox* mailbox = new EntityMailbox(e->getScriptModule(), NULL, componentID, entityID, MAILBOX_TYPE_BASE);
		e->setBaseMailbox(mailbox);
		
		// 添加到space
		space->addEntity(e);
		e->initializeEntity(params);
		Py_XDECREF(params);

		// 告知baseapp， entity的cell创建了
		Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
		pBundle->newMessage(BaseappInterface::onEntityGetCell);
		BaseappInterface::onEntityGetCellArgs3::staticAddToBundle(*pBundle, entityID, componentID_, spaceID);
		pBundle->send(this->getNetworkInterface(), cinfos->pChannel);
		Mercury::Bundle::ObjPool().reclaimObject(pBundle);

		// 如果是有client的entity则设置它的clientmailbox, baseapp部分的onEntityGetCell会告知客户端enterworld.
		if(hasClient)
		{
			// 初始化默认AOI范围
			ENGINE_COMPONENT_INFO& ecinfo = ServerConfig::getSingleton().getCellApp();
			e->setAoiRadius(ecinfo.defaultAoIRadius, ecinfo.defaultAoIHysteresisArea);

			e->onGetWitness(cinfos->pChannel);
		}

		return;
	}

	KBE_ASSERT(false && "Cellapp::onCreateCellEntityFromBaseapp: is error!\n");
}

//-------------------------------------------------------------------------------------
void Cellapp::onDestroyCellEntityFromBaseapp(Mercury::Channel* pChannel, ENTITY_ID eid)
{
	// DEBUG_MSG("Cellapp::onDestroyCellEntityFromBaseapp:entityID=%d.\n", eid);
	destroyEntity(eid);
}

//-------------------------------------------------------------------------------------
void Cellapp::onEntityMail(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	ENTITY_ID eid;
	s >> eid;

	ENTITY_MAILBOX_TYPE	mailtype;
	s >> mailtype;

	// 在本地区尝试查找该收件人信息， 看收件人是否属于本区域
	Entity* entity = pEntities_->find(eid);
	if(entity == NULL)
	{
		ERROR_MSG("Cellapp::onEntityMail: entityID %d not found.\n", eid);
		return;
	}
	
	Mercury::Bundle* pBundle = Mercury::Bundle::ObjPool().createObject();
	Mercury::Bundle& bundle = *pBundle;

	switch(mailtype)
	{
		case MAILBOX_TYPE_CELL:																		// 本组件是baseapp，那么确认邮件的目的地是这里， 那么执行最终操作
			entity->onRemoteMethodCall(pChannel, s);
			break;
		case MAILBOX_TYPE_BASE_VIA_CELL: // entity.base.cell.xxx
			{
				EntityMailboxAbstract* mailbox = static_cast<EntityMailboxAbstract*>(entity->getBaseMailbox());
				if(mailbox == NULL){
					ERROR_MSG("Cellapp::onEntityMail: occur a error(can't found baseMailbox)! mailboxType=%d, entityID=%d.\n", mailtype, eid);
					break;
				}
				
				mailbox->newMail(bundle);
				bundle.append(s);
				mailbox->postMail(bundle);
			}
			break;
		case MAILBOX_TYPE_CLIENT_VIA_CELL: // entity.cell.client
			{
				EntityMailboxAbstract* mailbox = static_cast<EntityMailboxAbstract*>(entity->getClientMailbox());
				if(mailbox == NULL){
					ERROR_MSG("Cellapp::onEntityMail: occur a error(can't found clientMailbox)! mailboxType=%d, entityID=%d.\n", mailtype, eid);
					break;
				}
				
				mailbox->newMail(bundle);
				bundle.append(s);
				s.read_skip(s.opsize());
				mailbox->postMail(bundle);
			}
			break;
		default:
			ERROR_MSG("Cellapp::onEntityMail: mailboxType %d is error! must a cellType. entityID=%d.\n", mailtype, eid);
	};

	Mercury::Bundle::ObjPool().reclaimObject(pBundle);
}

//-------------------------------------------------------------------------------------
void Cellapp::onRemoteCallMethodFromClient(Mercury::Channel* pChannel, KBEngine::MemoryStream& s)
{
	ENTITY_ID srcEntityID, targetID;

	s >> srcEntityID >> targetID;

	KBEngine::Entity* e = KBEngine::Cellapp::getSingleton().findEntity(targetID);		

	if(e == NULL)
	{	
		WARNING_MSG("Cellapp::onRemoteCallMethodFromClient: can't found entityID:%d, by srcEntityID:%d.\n", targetID, srcEntityID);
		return;
	}

	// 这个方法呼叫如果不是这个proxy自己的方法则必须呼叫的entity和proxy的cellEntity在一个space中。
	e->onRemoteMethodCall(pChannel, s);
}

//-------------------------------------------------------------------------------------
void Cellapp::forwardEntityMessageToCellappFromClient(Mercury::Channel* pChannel, MemoryStream& s)
{
	ENTITY_ID srcEntityID;

	s >> srcEntityID;

	KBEngine::Entity* e = KBEngine::Cellapp::getSingleton().findEntity(srcEntityID);		

	if(e == NULL)
	{	
		WARNING_MSG("Cellapp::forwardEntityMessageToCellappFromClient: can't found entityID:%d.\n", srcEntityID);
		return;
	}

	if(e->isDestroyed())																				
	{																										
		ERROR_MSG("%s::forwardEntityMessageToCellappFromClient: %d is destroyed!\n",											
			e->getScriptName(), e->getID());
		s.read_skip(s.opsize());
		return;																							
	}

	// 检查是否是entity消息， 否则不合法.
	while(s.opsize() > 0 && !e->isDestroyed())
	{
		Mercury::MessageID			currMsgID;
		Mercury::MessageLength		currMsgLen;

		s >> currMsgID;

		Mercury::MessageHandler* pMsgHandler = CellappInterface::messageHandlers.find(currMsgID);

		if(pMsgHandler == NULL)
		{
			ERROR_MSG("Cellapp::forwardEntityMessageToCellappFromClient: invalide msgID=%d, msglen=%d, from %s.\n", 
				currMsgID, s.wpos(), pChannel->c_str());

			pChannel->condemn();
			break;
		}

		if(pMsgHandler->type() != Mercury::MERCURY_MESSAGE_TYPE_ENTITY)
		{
			WARNING_MSG("Cellapp::forwardEntityMessageToCellappFromClient: msgID=%d not is entitymsg.\n", currMsgID);

			pChannel->condemn();
			break;
		}

		if((pMsgHandler->msgLen == MERCURY_VARIABLE_MESSAGE) || Mercury::g_packetAlwaysContainLength)
			s >> currMsgLen;
		else
			currMsgLen = pMsgHandler->msgLen;

		if(s.opsize() < currMsgLen || currMsgLen >  MERCURY_MESSAGE_MAX_SIZE / 2)
		{
			ERROR_MSG("Cellapp::forwardEntityMessageToCellappFromClient: msgID=%d, invalide msglen=%d, from %s.\n", 
				currMsgID, s.wpos(), pChannel->c_str());

			pChannel->condemn();
			break;
		}

		// 临时设置有效读取位， 防止接口中溢出操作
		size_t wpos = s.wpos();
		// size_t rpos = s.rpos();
		size_t frpos = s.rpos() + currMsgLen;
		s.wpos(frpos);
		pMsgHandler->handle(pChannel, s);

		// 防止handle中没有将数据导出获取非法操作
		if(currMsgLen > 0)
		{
			if(frpos != s.rpos())
			{
				CRITICAL_MSG("Cellapp::forwardEntityMessageToCellappFromClient[%s]: rpos(%d) invalid, expect=%d. msgID=%d, msglen=%d.\n",
					pMsgHandler->name.c_str(), s.rpos(), frpos, currMsgID, currMsgLen);

				s.rpos(frpos);
			}
		}

		s.wpos(wpos);
	}
}

//-------------------------------------------------------------------------------------

}
