/*
 * Copyright (C) 2011-2012 FlylinkDC++ Team http://flylinkdc.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "stdinc.h"
#ifdef SSA_VIDEO_PREVIEW_FEATURE
#include "VideoPreview.h"
#include "../client/ResourceManager.h"
#include "../client/LogManager.h"
#include "../client/QueueManager.h"
#include "../client/SharedFileStream.h"

#define MAX_INFO_STACK_COUNT 20

VideoPreview::~VideoPreview()
{
	/* [-] IRainman fix.
	SettingsManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	*/
}

void
VideoPreview::shutdown()
{
	clear();
	// [+] IRainman fix.
	SettingsManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);
	// [~] IRainman fix.
	addTask(SHUTDOWN, nullptr);
}

void VideoPreview::StartServer()
{
	addTask(START_PREVIEW_SERVER, nullptr);
}
void VideoPreview::StopServer()
{
	addTask(STOP_PREVIEW_SERVER, nullptr);
}
void VideoPreview::AddLogInfo(const std::string& p_loginfo)
{
	// [!] TODO: refactoring witout this.
	addTask(ADD_LOG_INFO, new TaskData(p_loginfo));
	// 2012-04-23_20-04-20_OZJ2EQXWZYW6V333A2W5HBCNLUGLVFTLC54Q7BQ_A0B55B4D_crash-stack-r502-beta21-x64-build-9811.dmp
	// 2012-04-27_18-47-20_DPP42GQ5GG7Y5Q45X5O6LX6QUQMJRZ7XQPGYBZA_609C4718_crash-stack-r502-beta22-x64-build-9854.dmp
} //[6] https://www.box.net/shared/805daddf39c3c4c631d3

void
VideoPreview::initialize()
{
	SettingsManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);
	// StartServer();
}

int
VideoPreview::run()
{
	while (true)
	{
		try
		{
			if (!checkEvents())
				break;
		}
		catch (const Exception& e)
		{
			fail(e.getError());
		}
	}
	return 0;
}

bool VideoPreview::checkEvents()
{
	while (taskSem.wait())
	{
		pair<Tasks, TaskData*> p;
		p.second = 0;
		{
			Lock l(cs); // [1] https://www.box.net/shared/8051e3afc6d6ee56fdfe
			dcassert(!m_tasks.empty());
			if (m_tasks.empty())  
				return false;
			p = m_tasks.front();
			m_tasks.pop_front();
		}
		try
		{
			if (failed && p.first != SHUTDOWN)
			{
				dcdebug("VideoPreview: New command when already failed: %d\n", p.first);
				fail(STRING(DISCONNECTED));
				goto check_events_clean_task_data; // [+] IRainman fix.
				// delete p.second; continue; [-] IRainman fix.
			}
			
			switch (p.first)
			{
				case START_PREVIEW_SERVER:
				{
					if (!IsServerStarted())
					{
						_StartServer();
					}	
				}
				break;
				case STOP_PREVIEW_SERVER:
				{
					if (IsServerStarted())
						_StopServer();		
				}
				break;
				case ADD_LOG_INFO:
				{
					_AddLogInfo(p.second->_logInfo);
				}
				break;
				case SHUTDOWN:
				{
					if (IsServerStarted())
						_StopServer();
				}
				return false;
			}			
			// delete p.second; [-] IRainman fix.
		}
		catch (const Exception& e)
		{
			// delete p.second; [-] IRainman fix.
			fail(e.getError());
		}
		// [+] IRainman fix.
	check_events_clean_task_data:
		delete p.second;
		// [~] IRainman fix.
	}
	return true;
}

void VideoPreview::clear()
{
	_callWnd = NULL;
	_viewStarted = true;
	if (!_currentFilePreview.empty())
	{
		QueueManager::LockFileQueueShared l_instance;
		auto qi = QueueManager::getInstance()->fileQueue.find(_currentFilePreview);
		if (qi)
			qi->setDelegate(NULL);
	}
	// Stop/Remove Items
	SocketProcessorSIter i = _socketProcessors.begin();
	while (i != _socketProcessors.end())
	{
		if ((*i)->IsInProcess()) // [2] https://www.box.net/shared/6sfnyx7a7wg6ig9a8uly
		{
			(*i)->setServerIsDie();
			(*i)->join();
			int resCount = 10;
			while ((*i)->IsInProcess())
			{
				// TODO: Cruth?
				if (--resCount < 0)
					break;
				::Sleep(10);
			}
		}
		VideoPreviewSocketProcessor* item = *i;
		i = _socketProcessors.erase(i);
		delete item;
	}
	
	_socketProcessors.clear();
	_ask2Download.clear();
}

void VideoPreview::AddExistingFileToPreview(const string& previewFile, const int64_t& previewFileSize, HWND serverReadyReport)
{
	_canUseFile = true;
	clear();
	_currentFilePreview = previewFile;
	_previewFileSize = previewFileSize;
	_tempFilename = previewFile;
	_fileRoadMap = unique_ptr<FileRoadMap>(new FileRoadMap(_previewFileSize));
	setFileAlreadyDownloaded();
	LogManager::getInstance()->message("Existing file preview - " + previewFile);
	StartServer();
	_viewStarted = true;
	if (::IsWindow(serverReadyReport))
		_ServerStartedReportNoWait(serverReadyReport);
}


void VideoPreview::AddTempFileToPreview(QueueItem* tempItem, HWND serverReadyReport)
{
	if (tempItem != NULL)
	{
		_canUseFile = false;
		clear();
		_callWnd = serverReadyReport;
		_currentFilePreview = tempItem->getTarget();
		_previewFileSize = tempItem->getSize();
		_tempFilename = tempItem->getTempTarget();
		_fileRoadMap = unique_ptr<FileRoadMap>(new FileRoadMap(_previewFileSize));
		QueueItem::SegmentSet segments =  tempItem->getDone();
		for (auto i = segments.cbegin(); i != segments.cend(); ++i)
		{
			addSegment(i->getStart(), i->getEnd());
		}
		tempItem->setDelegate(this);
		_canUseFile = true;
		StartServer();
		_viewStarted = false;
	}
}

void VideoPreview::fail(const string& aError)
{
	if (!failed)
	{
		failed = true;
		fire(VideoPreviewListener::Failed(), aError);
	}
}


void VideoPreview::_StartServer()
{
	if (_serverPreview == NULL)
	{
		_serverPreview = new PreviewServer(SETTING(INT_PREVIEW_SERVER_PORT), SETTING(BIND_ADDRESS));
		_serverStarted = true;
		_isServerDie = false;
		LogManager::getInstance()->message("Started PreviewServer");
		VideoPreview::getInstance()->AddLogInfo("Started PreviewServer");
	}
}
void VideoPreview::_StopServer()
{
	_isServerDie = true;
	if (_serverPreview != NULL)
	{
		safe_delete(_serverPreview);
		_serverStarted = false;
		LogManager::getInstance()->message("Stopped PreviewServer"); // 2012-04-27_18-47-20_DPP42GQ5GG7Y5Q45X5O6LX6QUQMJRZ7XQPGYBZA_609C4718_crash-stack-r502-beta22-x64-build-9854.dmp
#ifdef _DEBUG
		VideoPreview::getInstance()->AddLogInfo("Stopped PreviewServer"); // [20] https://www.box.net/shared/qzotdk8odzsct74kxzgg
#endif
	}
	clear();
}

VideoPreview::PreviewServer::PreviewServer(uint16_t aPort, const string& ip /* = "0.0.0.0" */) :
	port(0),
	die(false)
{
	sock.create();
	sock.setSocketOpt(SO_REUSEADDR, 1);
	dcassert(aPort);
	port = sock.bind(aPort, ip);
	sock.listen();
	start();
}


static const uint64_t POLL_TIMEOUT = 250;

int VideoPreview::PreviewServer::run() noexcept
{
	while (!die)
	{
		try
		{
			while (!die)
			{
				auto ret = sock.wait(POLL_TIMEOUT, Socket::WAIT_READ);
				if (ret == Socket::WAIT_READ)
				{
					VideoPreview::getInstance()->accept(sock);
				}
			}
		}
		catch (const Exception& e)
		{
			LogManager::getInstance()->message(STRING(LISTENER_FAILED) + ' ' + e.getError());
		}
		bool failed = false;
		while (!die)
		{
			try
			{
				sock.disconnect();
				sock.create();
				sock.bind(port, ip);
				sock.listen();
				if (failed)
				{
					LogManager::getInstance()->message(STRING(CONNECTIVITY_RESTORED));
					failed = false;
				}
				break;
			}
			catch (const SocketException& e)
			{
				dcdebug("VideoPreview::PreviewServer::run Stopped listening: %s\n", e.getError().c_str());
				
				if (!failed)
				{
					LogManager::getInstance()->message(STRING(CONNECTIVITY_ERROR) + ' ' + e.getError());
					failed = true;
				}
				// Spin for 60 seconds
				for (int i = 0; i < 60 && !die; ++i)
				{
					Thread::sleep(1000);
				}
			}
		}
	}
	return 0;
}


void VideoPreview::accept(const Socket& sock) noexcept
{
	VideoPreviewSocketProcessor* socketProcessor = new VideoPreviewSocketProcessor();
	socketProcessor->accept(sock);
	socketProcessor->start();
	_socketProcessors.push_back(socketProcessor);
}

bool VideoPreview::IsAvailableData(int64_t pos, int64_t size) const
{
	Lock l(csRoadMap);
	if (_fileRoadMap.get() != NULL)
		return _fileRoadMap->IsAvaiable(pos, size);
		
	return false;
}

void VideoPreview::addSegment(int64_t start, int64_t size)
{
	Lock l(csRoadMap);
	_fileRoadMap->AddSegment(start, size);
	if (!_viewStarted && ::IsWindow(_callWnd))
		_ServerStartedReportNoWait(_callWnd);
	_viewStarted = true;
}

void VideoPreview::setFileAlreadyDownloaded()
{
	Lock l(csRoadMap);
	_fileRoadMap->AddSegment(0, _previewFileSize);
}


void VideoPreview::SetDownloadSegment(int64_t pos, int64_t size)
{
	Lock l(cs_downloadItems);
	
	
	int64_t posNew = pos;
	int64_t sizeNew = size;
	bool iFound = false;
	if (!_fileRoadMap->GetInsertableSizeAndPos(posNew, sizeNew))
	{
		MapVItems::const_iterator i = _ask2Download.cbegin();
		while (i != _ask2Download.cend()) // ��� ���� ������ ����� ��� ��������� �� ������ �� �������� �� ���������
		{
			if (IsAvailableData(i->getPosition(), i->getSize()))
				i = _ask2Download.erase(i);
			else
			{
				if (i->getPosition() == posNew)
					iFound = true;
				++i;
			}
		}
		if (iFound)
			return;
			
		LocalArray<CHAR, 256>buff;
		sprintf(buff.data(), "ask segment: %s / %s", Util::toString(posNew).c_str(), Util::toString(sizeNew).c_str());
		string loginfo = buff.data();
		VideoPreview::getInstance()->AddLogInfo(loginfo);
		_ask2Download.push_back(FileRoadMapItem(posNew, sizeNew));
	}
}

void VideoPreview::on(QueueManagerListener::TryFileMoving, const string& n) noexcept
{
	if (n.compare(_currentFilePreview) == 0)
	{
		// Let's try to stop all functions with file untill removed
		_canUseFile = false;
		::Sleep(1000);
	}
}

void VideoPreview::on(QueueManagerListener::FileMoved, const string& n) noexcept
{
	if (n.compare(_currentFilePreview) == 0)
	{
		Lock l(csRoadMap);
		_tempFilename = n;
		_canUseFile = true;
		
		LocalArray<CHAR, 256>buff;
		sprintf(buff.data(), "All file %s now available", n.c_str());
		string loginfo = buff.data();
		VideoPreview::getInstance()->AddLogInfo(loginfo);
		
	}
}

size_t VideoPreview::getDownloadItems(int64_t blockSize, vector<int64_t>& ItemsArray)
{
	Lock l(cs_downloadItems);
	MapVItems::const_iterator i = _ask2Download.cbegin();
	while (i != _ask2Download.cend())
	{
		if (IsAvailableData(i->getPosition(), i->getSize()))
			i = _ask2Download.erase(i);
		else
		{
			int64_t startPos = ((i->getPosition()) / blockSize) * blockSize;
			const int64_t endPos = ((i->getPosition() + i->getSize()) / blockSize) * blockSize;
			for (; startPos <= endPos; startPos += blockSize)
				ItemsArray.push_back(startPos);

			++i;
		}
		
	}
	
	_ask2Download.clear();
	return ItemsArray.size();
}

void VideoPreview::setDownloadItem(int64_t pos, int64_t blockSize)
{
}

int VideoPreviewSocketProcessor::run()
{
	// ReadHeaders from socket.
	char buff[8192];
	memzero(buff, sizeof(buff));
	::Sleep(1);
	
	int size = recv(sock, buff, sizeof(buff), 0);
	if (size == SOCKET_ERROR)
	{
		int error = ::WSAGetLastError();
		while (error == WSAEWOULDBLOCK)
		{
			::Sleep(1);
			size = recv(sock, buff, sizeof(buff), 0);
			error = 0;
			if (size == SOCKET_ERROR)
				error =  ::WSAGetLastError();
		}
	}
	if (size == SOCKET_ERROR)
		return 0;
		
	string header = buff;
	header = header.substr(0, static_cast<size_t>(size));
	
	parseHeader(header);
	size_t BlockSize = SETTING(INT_PREVIEW_SERVER_SPEED) * 1024;
	if (BlockSize == 0)
		BlockSize = 500 * 1024;
		
	int64_t realFileDataLength = VideoPreview::getInstance()->GetFilePreviewSize();
	int64_t startValue = 0;
	int64_t endValue =  realFileDataLength - 1;
	
	HeaderMapIter iter = headers.find("Range");
	if (iter != headers.end())
	{
		string parsebytesValue = iter->second;
		// bytes=1465141248-
		if (parsebytesValue.substr(0, 6).compare("bytes=") == 0)
		{
			string realbytesRange = parsebytesValue.substr(6);
			size_t lastIndex = realbytesRange.find('-');
			if (lastIndex != string::npos)
			{
				string startValueStr = realbytesRange.substr(0, lastIndex);
				startValue = Util::toInt64(startValueStr);
				if (startValue < 0 || startValue > realFileDataLength - 1)
					startValue = 0;
				size_t realbytesRangeLen = realbytesRange.length();
				if (realbytesRangeLen > lastIndex + 1)
				{
					string endValueStr = realbytesRange.substr(lastIndex + 1);
					if (!endValueStr.empty())
					{
						endValue = Util::toInt64(endValueStr);
						if (endValue < 0 || endValue > realFileDataLength - 1)
							endValue = realFileDataLength - 1;
					}
				}
			}
		}
	}
	
	long dataLength = endValue - startValue + 1;
	
	// dcdebug("VideoPreviewSocketProcessor Ranger: %s - %s\n", Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
	string clientName;
	iter = headers.find("User-Agent");
	if (iter != headers.end())
		clientName = iter->second;
		
	sprintf(buff, "Client %s asks range %s - %s", clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
	string loginfo = buff;
	VideoPreview::getInstance()->AddLogInfo(loginfo);
	
	
	// Keep_Alive?
	//DWORD optval = TRUE;
	//int optlen = sizeof(optval);
	//int test = 0;
	//test = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (char*)&optval, optlen);
	
	try
	{
		// Accept new socket thread
		if (VideoPreview::getInstance() != NULL && endValue > 0)
		{
			std::string content_type = "application/avi";
			
			std::string data = "HTTP/1.1 200 OK\nContent-type: ";
			data += content_type;
			data += "\nContent-Encoding: 8bit";
			if (VideoPreview::getInstance()->IsPreviewFileExists() && BOOLSETTING(INT_PREVIEW_USE_VIDEO_SCROLL))
			{
				data += "\nAccept-Ranges: bytes";
				data += "\nContent-Length:";
				data += Util::toString(dataLength);
				data += "\nContent-Range: bytes ";
				data += Util::toString(startValue);
				data += '-';
				data += Util::toString(endValue);
				data += '/';
				data += Util::toString(realFileDataLength);
			}
			data += "\nContent-Disposition: attachment; filename=";
			data += Util::getFileName(VideoPreview::getInstance()->GetFilePreviewName());
			data += "\n\n";
			
			int64_t filePosition = startValue;
			
			if (SOCKET_ERROR != ::send(sock, data.c_str(), static_cast<int>(data.size()), 0))
			{
				bool dataAsks = false;
				while (!isServerDie)
				{
					if (filePosition >= endValue)
					{
						sprintf(buff, "Finished sending for client %s (%s - %s)", clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
						loginfo = buff;
						VideoPreview::getInstance()->AddLogInfo(loginfo);
						break;
					}
					if (VideoPreview::getInstance()->CanUseFile())
					{
						unique_ptr<SharedFileStream> openedFile(new SharedFileStream(VideoPreview::getInstance()->GetFilePreviewTempName(),  File::READ, File::OPEN | File::CREATE | File::NO_CACHE_HINT));
						if (openedFile.get() != NULL)
						{
							if (VideoPreview::getInstance()->IsAvailableData(filePosition, BlockSize))
							{
								if (dataAsks)
								{
									sprintf(buff, "Data was downloaded for client %s (%s - %s)", clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
									loginfo = buff;
									VideoPreview::getInstance()->AddLogInfo(loginfo);
								}
								dataAsks = false;
								openedFile->setPos(filePosition);
								size_t readSize = BlockSize;
								unique_ptr<uint8_t[]> data(new uint8_t[readSize]);
								readSize = openedFile->read((void*)data.get(), readSize);
								openedFile.reset();
								filePosition += readSize;
								if (readSize == 0)
								{
									sprintf(buff, "Finished sending for client %s (%s - %s)", clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
									loginfo = buff;
									VideoPreview::getInstance()->AddLogInfo(loginfo);
									break;
								}
								
								// dcdebug("VideoPreviewSocketProcessor SENDING Ranger: %s - %s\n", Util::toString(filePosition).c_str(), Util::toString(readSize).c_str());
								sprintf(buff, "Sending %s - %s for client %s (%s - %s)", Util::toString(filePosition).c_str(), Util::toString(readSize).c_str(), clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
								loginfo = buff;
								VideoPreview::getInstance()->AddLogInfo(loginfo);
								
								int result = ::send(sock, (const char*)data.get(), static_cast<int>(readSize), 0);
								if (result == SOCKET_ERROR)
								{
									int error = ::WSAGetLastError();
									while (error == WSAEWOULDBLOCK)
									{
										::Sleep(1);
										result = ::send(sock, (const char*)data.get(), static_cast<int>(readSize), 0);
										error = 0;
										if (result == SOCKET_ERROR)
											error =  ::WSAGetLastError();
									}
								}
								if (SOCKET_ERROR == result)
									break;
								::Sleep(1000);
							}
							else
							{
								openedFile.reset();
								if (!dataAsks)
								{
									VideoPreview::getInstance()->SetDownloadSegment(filePosition, BlockSize);
									dataAsks = true;
								}
								::Sleep(1000);
							}
						}
						else
						{
							::Sleep(1);
						}
					}
					else
					{
						::Sleep(1000);
					}
				}
			}
		}
	}
	catch (...)
	{
	}
	
	::closesocket(sock);
	inProcess = false;
	
	sprintf(buff, "SOCKET Closed by Client %s range %s - %s", clientName.c_str(), Util::toString(startValue).c_str(), Util::toString(endValue).c_str());
	loginfo = buff;
	VideoPreview::getInstance()->AddLogInfo(loginfo);
	
	
	return 0;
}

void VideoPreviewSocketProcessor::parseHeader(const string& headerString)
{
	headers.clear();
	
	bool hasKey = false;
	bool hasValue = false;
	string key;
	string value;
	for (size_t i = 0; i < headerString.length(); i++)
	{
		if (headerString[i] == '\n')
		{
			hasValue = true;
			hasKey = true;
		}
		else if (headerString[i] == ':' && !hasKey)
		{
			hasKey = true;
		}
		else if (!hasKey)
		{
			key += headerString[i];
		}
		else
		{
			value += headerString[i];
		}
		
		if (hasValue && hasKey)
		{
			if (!key.empty() && key.length() > 1)
			{
				HeaderMapIter iter = headers.find(key);
				if (iter == headers.end())
				{
					if (value.length() > 0 && value[0] == ' ')
						value = value.substr(1);
					if (value.length() > 0 && (value[value.length() - 1] == '\n' || value[value.length() - 1] == '\r'))
						value = value.substr(0, value.length() - 1); // Remove last \n \r
					headers.insert(pair<string, string>(key, value));
				}
				else
				{
					iter->second = value;
				}
			}
			key.clear();
			value.clear();
			hasKey = false;
			hasValue = false;
		}
	}
}

void VideoPreview::_AddLogInfo(const std::string& loginfo)
{
	Lock l(csInfo);
	if (_logInfoData.size() >= MAX_INFO_STACK_COUNT)
		_logInfoData.pop();
		
	_logInfoData.push(loginfo);
	_SendLogInfo();
}

bool VideoPreview::GetNextLogItem(string& outString)
{
	Lock l(csInfo);
	if (_logInfoData.size())
	{
		outString = _logInfoData.front();
		_logInfoData.pop();
	}
	else
		outString.clear();
	
	return !outString.empty();
}

#endif // SSA_VIDEO_PREVIEW_FEATURE