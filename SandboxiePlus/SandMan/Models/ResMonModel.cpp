#include "stdafx.h"
#include "ResMonModel.h"
#include "../MiscHelpers/Common/Common.h"
#include "../SbiePlusAPI.h"

CResMonModel::CResMonModel(QObject *parent)
:CListItemModel(parent)
{
}

CResMonModel::~CResMonModel()
{
}

//void CResMonModel::Sync(const QList<CTraceEntryPtr>& List, QSet<quint64> PIDs)
void CResMonModel::Sync(const QList<CTraceEntryPtr>& List)
{
	QList<SListNode*> New;
	QHash<QVariant, SListNode*> Old = m_Map;

	int i = 0;
	if (List.count() >= m_List.count() && m_List.count() > 0)
	{
		i = m_List.count() - 1;
		if (m_List.at(i)->ID == List.at(i)->GetUID())
		{
			i++;
			Old.clear();
		}
		else
			i = 0;
	}
	
	for(; i < List.count(); i++)
	{
		CTraceEntryPtr pEntry = List.at(i);

		QVariant ID = pEntry->GetUID();

		//if (!PIDs.isEmpty() && !PIDs.contains(pEntry->GetProcessId()))
		//	continue;

		int Row = -1;
		QHash<QVariant, SListNode*>::iterator I = Old.find(ID);
		STraceNode* pNode = I != Old.end() ? static_cast<STraceNode*>(I.value()) : NULL;
		if(!pNode)
		{
			pNode = static_cast<STraceNode*>(MkNode(ID));
			pNode->Values.resize(columnCount());
			pNode->pEntry = pEntry;
			New.append(pNode);
		}
		else
		{
			I.value() = NULL;
			Row = GetRow(pNode);
		}

		int Col = 0;
		bool State = false;
		int Changed = 0;

		/*int RowColor = CTaskExplorer::eNone;
		if (pGDI->IsMarkedForRemoval() && CTaskExplorer::UseListColor(CTaskExplorer::eToBeRemoved))		RowColor = CTaskExplorer::eToBeRemoved;
		else if (pGDI->IsNewlyCreated() && CTaskExplorer::UseListColor(CTaskExplorer::eAdded))			RowColor = CTaskExplorer::eAdded;

		if (pNode->iColor != RowColor) {
			pNode->iColor = RowColor;
			pNode->Color = CTaskExplorer::GetListColor(RowColor);
			Changed = 2;
		}*/

		for(int section = 0; section < columnCount(); section++)
		{
			if (!m_Columns.contains(section))
				continue; // ignore columns which are hidden

			QVariant Value;
			switch(section)
			{
				case eProcess:			Value = pEntry->GetProcessId(); break;
				case eTimeStamp:		Value = pEntry->GetUID(); break;
				case eType:				Value = pEntry->GetTypeStr(); break;
				case eStatus:			Value = pEntry->GetStautsStr(); break;
				case eValue:			Value = pEntry->GetMessage(); break;
			}

			STraceNode::SValue& ColValue = pNode->Values[section];

			if (ColValue.Raw != Value)
			{
				if(Changed == 0)
					Changed = 1;
				ColValue.Raw = Value;

				switch (section)
				{
					case eProcess:			
					{
						CBoxedProcessPtr pProcess = theAPI->GetProcessById(pEntry->GetProcessId());
						ColValue.Formated = QString("%1 (%2, %3)").arg(pProcess.isNull() ? tr("Unknown") : pProcess->GetProcessName()).arg(pEntry->GetProcessId()).arg(pEntry->GetThreadId());
						break;
					}
					case eTimeStamp:		ColValue.Formated = pEntry->GetTimeStamp().toString("hh:mm:ss.zzz"); break;
					//case eType:			ColValue.Formated = ; break;
					//case eValue:			ColValue.Formated = ; break;
				}
			}

			if(State != (Changed != 0))
			{
				if(State && Row != -1)
					emit dataChanged(createIndex(Row, Col), createIndex(Row, section-1));
				State = (Changed != 0);
				Col = section;
			}
			if(Changed == 1)
				Changed = 0;
		}
		if(State && Row != -1)
			emit dataChanged(createIndex(Row, Col, pNode), createIndex(Row, columnCount()-1, pNode));

	}

	CListItemModel::Sync(New, Old);
}

CTraceEntryPtr CResMonModel::GetEntry(const QModelIndex &index) const
{
	if (!index.isValid())
        return CTraceEntryPtr();

	STraceNode* pNode = static_cast<STraceNode*>(index.internalPointer());
	return pNode->pEntry;
}

int CResMonModel::columnCount(const QModelIndex &parent) const
{
	return eCount;
}

QVariant CResMonModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
	{
		switch(section)
		{
			case eProcess:			return tr("Process");
			case eTimeStamp:		return tr("Time Stamp");
			case eType:				return tr("Type");
			case eStatus:			return tr("Status");
			case eValue:			return tr("Value");
		}
	}
    return QVariant();
}
