/*
 * Copyright (C) 2001-2013 Jacek Sieka, arnetheduck on gmail point com
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
#include "Download.h"

#include "UserConnection.h"
#include "QueueItem.h"
#include "HashManager.h"

// [!] IRainman fix.
Download::Download(UserConnection& conn, QueueItem* item) noexcept :
#ifdef PPA_INCLUDE_DROP_SLOW
lastNormalSpeed(0),
#endif
                qi(item),
                Transfer(conn, item->getTarget(), item->getTTH()),
                file(nullptr), treeValid(false)
{
	qi->inc(); // [+]
	// [~] IRainman fix.
	
	conn.setDownload(this);
	
	// [-] QueueItem::SourceConstIter source = qi.getSource(getUser()); [-] IRainman fix.
	
	if (qi->isSet(QueueItem::FLAG_PARTIAL_LIST))
	{
		setType(TYPE_PARTIAL_LIST);
		//pfs = qi->getTarget(); // [+] IRainman fix. TODO: ���������, �� �� �� ����� :(
	}
	else if (qi->isSet(QueueItem::FLAG_USER_LIST))
	{
		setType(TYPE_FULL_LIST);
	}
	
	if (qi->isSet(QueueItem::FLAG_USER_CHECK))
		setFlag(FLAG_USER_CHECK);
	// [+] SSA
	if (qi->isSet(QueueItem::FLAG_USER_GET_IP))
		setFlag(FLAG_USER_GET_IP);
		
	// TODO: ������ ������ ����� ������������, ��� ��� �� ����� ���������� https://code.google.com/p/flylinkdc/issues/detail?id=1028
	const bool l_is_type_file = getType() == TYPE_FILE && qi->getSize() != -1;
	const bool l_is_check_tth = l_is_type_file && CFlylinkDBManager::getInstance()->getTree(getTTH(), getTigerTree());
	// ~TODO
	
	UniqueLock l(QueueItem::cs); // [+] IRainman fix.
	const QueueItem::SourceConstIter source = qi->getSourceL(getUser()); // [+] IRainman fix.
	
	if (source->isSet(QueueItem::Source::FLAG_PARTIAL))
		setFlag(FLAG_PARTIAL);
		
	if (l_is_type_file)
	{
		if (l_is_check_tth)
		{
			setTreeValid(true);
			setSegment(qi->getNextSegmentL(getTigerTree().getBlockSize(), conn.getChunkSize(), conn.getSpeed(), source->getPartialSource()));
		}
		else if (conn.isSet(UserConnection::FLAG_SUPPORTS_TTHL) && !source->isSet(QueueItem::Source::FLAG_NO_TREE) && qi->getSize() > MIN_BLOCK_SIZE) // [!] IRainman opt.
		{
			// Get the tree unless the file is small (for small files, we'd probably only get the root anyway)
			setType(TYPE_TREE);
			getTigerTree().setFileSize(qi->getSize());
			setSegment(Segment(0, -1));
		}
		else
		{
			// Use the root as tree to get some sort of validation at least...
			getTigerTree() = TigerTree(qi->getSize(), qi->getSize(), getTTH());
			setTreeValid(true);
			setSegment(qi->getNextSegmentL(getTigerTree().getBlockSize(), 0, 0, source->getPartialSource()));
		}
		
		if ((getStartPos() + getSize()) != qi->getSize())
		{
			setFlag(FLAG_CHUNKED);
		}
		
		if (getSegment().getOverlapped())
		{
			setFlag(FLAG_OVERLAP);
			qi->setOverlappedL(getSegment(), true); // [!] IRainman fix.
		}
	}
}

Download::~Download()
{
	getUserConnection().setDownload(nullptr);
	qi->dec(); // [+] IRainman fix.
}

AdcCommand Download::getCommand(bool zlib) const
{
	AdcCommand cmd(AdcCommand::CMD_GET);
	
	cmd.addParam(Transfer::g_type_names[getType()]);
	
	if (getType() == TYPE_PARTIAL_LIST)
	{
		cmd.addParam(Util::toAdcFile(getTempTarget()));
	}
	else if (getType() == TYPE_FULL_LIST)
	{
		if (isSet(Download::FLAG_XML_BZ_LIST))
		{
			cmd.addParam(g_user_list_name_bz);
		}
		else
		{
			cmd.addParam(g_user_list_name);
		}
	}
	else
	{
		cmd.addParam("TTH/" + getTTH().toBase32());
	}
	
	cmd.addParam(Util::toString(getStartPos()));
	cmd.addParam(Util::toString(getSize()));
	
	if (zlib && BOOLSETTING(COMPRESS_TRANSFERS))
	{
		cmd.addParam("ZL1");
	}
	
	return cmd;
}

void Download::getParams(const UserConnection& aSource, StringMap& params)
{
	Transfer::getParams(aSource, params);
	params["target"] = getPath();
}