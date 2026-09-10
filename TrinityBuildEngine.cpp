// TrinityBuildEngine.cpp
#include "StdAfx.h"
#include "TrinityBuildEngine.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityAttributeBuilder.h"

AcDbObjectId TrinityBuildEngine::ensureExists(const std::string& code, const AcGePoint3d& position,
                                                const TrinityRotationCompound& rotation,
                                                AcDbDatabase* targetDb, int depth) {
    if (depth > 20) return AcDbObjectId::kNull;
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if (!pNeuron) return AcDbObjectId::kNull;
    TrinityNeuron neuron = *pNeuron; delete pNeuron;
    std::string subdir = (neuron.type == "detail") ? m_files.detailsDir() :
                         (neuron.type == "assembly" || neuron.type == "construction") ? m_files.assembliesDir() :
                         m_files.projectsDir();
    std::string filePath = m_files.getFilePath(neuron.code, subdir);
    if (m_files.fileExists(neuron.code, subdir))
        return m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);

    wchar_t* wCode = utf2uni(neuron.code.c_str());
    acutPrintf(_T("\n[BuildEngine] BUILDING: %s (depth=%d)\n"), wCode, depth);
    free(wCode);

    if (neuron.type == "detail") {
        AcDbDatabase* cleanDb = buildDetail(neuron);
        if (!cleanDb) return AcDbObjectId::kNull;
        m_files.saveDwg(cleanDb, filePath);
        delete cleanDb;
        Sleep(200);
        return m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);
    }

    AcDbDatabase* cleanDb = buildDwg(neuron, depth);
    if (!cleanDb) return AcDbObjectId::kNull;
    m_files.saveDwg(cleanDb, filePath);
    delete cleanDb;
    Sleep(200);
    return m_files.attachXref(filePath, neuron.code, position, rotation, targetDb);
}

AcDbDatabase* TrinityBuildEngine::buildDetail(const TrinityNeuron& detail) {
    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
    TrinityLayerManager::createOrGetLayer(tempDb, detail.material);
    AcDb3dSolid* solid = TrinityGeometryBuilder::build(detail);
    if (!solid) { delete tempDb; return nullptr; }
    std::string layer = TrinityLayerManager::layerName(detail.material);
    wchar_t layerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, layerW, 256);
    solid->setLayer(layerW);

    AcDbBlockTable* pBt = nullptr; tempDb->getSymbolTable(pBt, AcDb::kForRead);
    AcDbBlockTableRecord* pMs = nullptr; pBt->getAt(ACDB_MODEL_SPACE, pMs, AcDb::kForWrite);
    pBt->close();
    AcDbObjectIdArray ids;
    AcDbObjectId solidId; pMs->appendAcDbEntity(solidId, solid);
    ids.append(solidId);

    if (detail.category == "rib") {
        TrinityLayerManager::ensureBoltLayer(tempDb);
        TrinityGeometryBuilder::drawBoltMarkers(detail, pMs, ids);
    }
    if (detail.category == "sidewall") {
        double holeRadius = 4.0, height = detail.thickness + 2.0;
        std::vector<AcGePoint3d> holePositions;
        size_t holesPos = detail.jsonData.find("\"holes\"");
        if (holesPos != std::string::npos) {
            size_t arrStart = detail.jsonData.find('[', holesPos);
            size_t arrEnd = detail.jsonData.find(']', arrStart);
            if (arrStart != std::string::npos && arrEnd != std::string::npos) {
                std::string holesStr = detail.jsonData.substr(arrStart, arrEnd - arrStart + 1);
                size_t objPos = 0;
                while ((objPos = holesStr.find("\"x\"", objPos)) != std::string::npos) {
                    double x=0,y=0;
                    size_t xVal=holesStr.find(':',objPos);
                    if(xVal!=std::string::npos) x=atof(holesStr.c_str()+xVal+1);
                    size_t yPos=holesStr.find("\"y\"",objPos);
                    if(yPos!=std::string::npos) { size_t yVal=holesStr.find(':',yPos); if(yVal!=std::string::npos) y=atof(holesStr.c_str()+yVal+1); }
                    holePositions.push_back(AcGePoint3d(x,y,0));
                    objPos=yPos+1;
                }
            }
        }
        for(const auto& pos : holePositions) {
            AcDb3dSolid* pCyl = new AcDb3dSolid();
            pCyl->createFrustum(height, holeRadius, holeRadius, holeRadius);
            AcGeMatrix3d mat; mat.setToIdentity();
            mat.setTranslation(AcGeVector3d(pos.x, pos.y, detail.thickness/2.0));
            pCyl->transformBy(mat);
            if(solid->booleanOper(AcDb::kBoolSubtract, pCyl) == Acad::eOk) pCyl->erase(); else delete pCyl;
        }
    }

    solid->close();
    TrinityAttributeBuilder::ensureTagLayer(tempDb);
    AcDbObjectId attrId = TrinityAttributeBuilder::addDetailCode(pMs, detail.code);
    if(attrId != AcDbObjectId::kNull) ids.append(attrId);
    pMs->close();

    AcDbDatabase* cleanDb = nullptr;
    if(tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin) != Acad::eOk || !cleanDb) { delete tempDb; return nullptr; }
    delete tempDb;
    TrinityLayerManager::createOrGetLayer(cleanDb, detail.material);
    TrinityAttributeBuilder::ensureTagLayer(cleanDb);

    AcDbBlockTable* pBt2=nullptr; cleanDb->getSymbolTable(pBt2,AcDb::kForRead);
    AcDbBlockTableRecord* pMs2=nullptr; pBt2->getAt(ACDB_MODEL_SPACE,pMs2,AcDb::kForWrite); pBt2->close();
    AcDbBlockTableRecordIterator* pIter=nullptr; pMs2->newIterator(pIter);
    if(pIter){
        wchar_t matLayerW[256]; MultiByteToWideChar(CP_UTF8,0,layer.c_str(),-1,matLayerW,256);
        for(pIter->start();!pIter->done();pIter->step()){
            AcDbEntity* pEnt=nullptr;
            if(pIter->getEntity(pEnt,AcDb::kForWrite)==Acad::eOk && pEnt){
                if(pEnt->isKindOf(AcDb3dSolid::desc())) pEnt->setLayer(matLayerW);
                else if(pEnt->isKindOf(AcDbCircle::desc())) pEnt->setLayer(_T("_bolt"));
                else pEnt->setLayer(_T("_tag"));
                pEnt->close();
            }
        }
        delete pIter;
    }
    pMs2->close();
    return cleanDb;
}

AcDbDatabase* TrinityBuildEngine::buildDwg(const TrinityNeuron& neuron, int depth) {
    if(neuron.type=="detail") return buildDetail(neuron);
    AcDbDatabase* tempDb = new AcDbDatabase(Adesk::kTrue,Adesk::kTrue);
    AcDbBlockTable* pBt=nullptr; tempDb->getSymbolTable(pBt,AcDb::kForRead);
    AcDbBlockTableRecord* pMs=nullptr; pBt->getAt(ACDB_MODEL_SPACE,pMs,AcDb::kForWrite); pBt->close();
    AcDbObjectIdArray ids;
    auto children = m_core.loadChildren(neuron.id);
    for(auto& syn : children){
        AcGePoint3d childPos = syn.position.toAcGe();
        std::string childFilePath = ensureFileExists(syn.childCode);
        if(childFilePath.empty()) continue;
        AcDbObjectId childId = m_files.attachXref(childFilePath, syn.childCode, childPos, syn.rotation, tempDb);
        if(childId != AcDbObjectId::kNull) ids.append(childId);
    }
    pMs->close();
    if(ids.isEmpty()){ delete tempDb; return nullptr; }
    AcDbDatabase* cleanDb = nullptr;
    if(tempDb->wblock(cleanDb, ids, AcGePoint3d::kOrigin) != Acad::eOk || !cleanDb){ delete tempDb; return nullptr; }
    delete tempDb;
    return cleanDb;
}

std::string TrinityBuildEngine::ensureFileExists(const std::string& code) {
    TrinityNeuron* pNeuron = m_core.loadNeuronByCode(code);
    if(!pNeuron) return "";
    TrinityNeuron neuron = *pNeuron; delete pNeuron;
    std::string subdir = (neuron.type=="detail")?m_files.detailsDir():m_files.assembliesDir();
    std::string filePath = m_files.getFilePath(neuron.code, subdir);
    if(m_files.fileExists(neuron.code, subdir)) return filePath;
    AcDbDatabase* db = (neuron.type=="detail")?buildDetail(neuron):buildDwg(neuron,0);
    if(!db) return "";
    m_files.saveDwg(db, filePath);
    delete db;
    return filePath;
}

int TrinityBuildEngine::processAllProjects(AcDbDatabase* targetDb) {
    auto projects = m_core.loadPendingProjects();
    if(projects.empty()) return 0;
    for(auto& proj : projects){
        AcGePoint3d origin(0,0,0);
        TrinityRotationCompound noRot;
        AcDbObjectId result = ensureExists(proj.code, origin, noRot, targetDb);
        if(result != AcDbObjectId::kNull) m_core.markNeuronDone(proj.id);
    }
    return (int)projects.size();
}