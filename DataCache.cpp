// DataCache.cpp
#include "StdAfx.h"
#include "DataCache.h"
#include "TrinityCore.h"

DataCache* DataCache::m_instance = nullptr;

DataCache* DataCache::getInstance()
{
    if (!m_instance)
        m_instance = new DataCache();
    return m_instance;
}

void DataCache::destroyInstance()
{
    delete m_instance;
    m_instance = nullptr;
}

void DataCache::clear()
{
    m_nodes.clear();
    m_parts.clear();
}

void DataCache::addNode(const NodeData& node)
{
    m_nodes[node.nodeId] = node;
}

void DataCache::addPart(const PartData& part)
{
    m_parts[part.partId] = part;
}

bool DataCache::getNode(int nodeId, NodeData& outNode) const
{
    auto it = m_nodes.find(nodeId);
    if (it != m_nodes.end())
    {
        outNode = it->second;
        return true;
    }
    return false;
}

bool DataCache::getPart(int partId, PartData& outPart) const
{
    auto it = m_parts.find(partId);
    if (it != m_parts.end())
    {
        outPart = it->second;
        return true;
    }
    return false;
}

size_t DataCache::getNodeCount() const
{
    return m_nodes.size();
}

size_t DataCache::getPartCount() const
{
    return m_parts.size();
}
