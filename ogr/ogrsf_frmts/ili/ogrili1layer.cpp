/******************************************************************************
 *
 * Project:  Interlis 1 Translator
 * Purpose:  Implements OGRILI1Layer class.
 * Author:   Pirmin Kalberer, Sourcepole AG
 *
 ******************************************************************************
 * Copyright (c) 2004, Pirmin Kalberer, Sourcepole AG
 * Copyright (c) 2008-2013, Even Rouault <even dot rouault at spatialys.com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "cpl_conv.h"
#include "cpl_string.h"
#include "ogr_geos.h"
#include "ogr_ili1.h"

#include <map>
#include <vector>

/************************************************************************/
/*                           OGRILI1Layer()                              */
/************************************************************************/

OGRILI1Layer::OGRILI1Layer(OGRFeatureDefn *poFeatureDefnIn,
                           const GeomFieldInfos &oGeomFieldInfosIn,
                           OGRILI1DataSource *poDSIn)
    : poFeatureDefn(poFeatureDefnIn), oGeomFieldInfos(oGeomFieldInfosIn),
      nFeatures(0), papoFeatures(nullptr), nFeatureIdx(0), bGeomsJoined(FALSE),
      poDS(poDSIn)
{
    SetDescription(poFeatureDefn->GetName());
    poFeatureDefn->Reference();
}

/************************************************************************/
/*                           ~OGRILI1Layer()                           */
/************************************************************************/

OGRILI1Layer::~OGRILI1Layer()
{
    for (int i = 0; i < nFeatures; i++)
    {
        delete papoFeatures[i];
    }
    CPLFree(papoFeatures);

    if (poFeatureDefn)
        poFeatureDefn->Release();
}

OGRErr OGRILI1Layer::AddFeature(OGRFeature *poFeature)
{
    nFeatures++;

    papoFeatures = static_cast<OGRFeature **>(
        CPLRealloc(papoFeatures, sizeof(void *) * nFeatures));

    papoFeatures[nFeatures - 1] = poFeature;

    return OGRERR_NONE;
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRILI1Layer::ResetReading()
{
    nFeatureIdx = 0;
}

/************************************************************************/
/*                           GetNextFeature()                           */
/************************************************************************/

OGRFeature *OGRILI1Layer::GetNextFeature()
{
    if (!bGeomsJoined)
        JoinGeomLayers();

    while (nFeatureIdx < nFeatures)
    {
        OGRFeature *poFeature = GetNextFeatureRef();
        if (poFeature)
            return poFeature->Clone();
    }
    return nullptr;
}

OGRFeature *OGRILI1Layer::GetNextFeatureRef()
{
    OGRFeature *poFeature = nullptr;
    if (nFeatureIdx < nFeatures)
    {
        poFeature = papoFeatures[nFeatureIdx++];
        // apply filters
        if ((m_poFilterGeom == nullptr ||
             FilterGeometry(poFeature->GetGeometryRef())) &&
            (m_poAttrQuery == nullptr || m_poAttrQuery->Evaluate(poFeature)))
            return poFeature;
    }
    return nullptr;
}

/************************************************************************/
/*                             GetFeatureRef()                          */
/************************************************************************/

OGRFeature *OGRILI1Layer::GetFeatureRef(GIntBig nFID)

{
    ResetReading();

    OGRFeature *poFeature = nullptr;
    while ((poFeature = GetNextFeatureRef()) != nullptr)
    {
        if (poFeature->GetFID() == nFID)
            return poFeature;
    }

    return nullptr;
}

OGRFeature *OGRILI1Layer::GetFeatureRef(const char *fid)

{
    ResetReading();

    OGRFeature *poFeature = nullptr;
    while ((poFeature = GetNextFeatureRef()) != nullptr)
    {
        if (!strcmp(poFeature->GetFieldAsString(0), fid))
            return poFeature;
    }

    return nullptr;
}

/************************************************************************/
/*                          GetFeatureCount()                           */
/************************************************************************/

GIntBig OGRILI1Layer::GetFeatureCount(int bForce)
{
    if (m_poFilterGeom == nullptr && m_poAttrQuery == nullptr
        /* && poAreaLineLayer == NULL*/)
    {
        return nFeatures;
    }
    else
    {
        return OGRLayer::GetFeatureCount(bForce);
    }
}

#ifndef d2str_defined
#define d2str_defined

static const char *d2str(double val)
{
    if (val == (int)val)
        return CPLSPrintf("%d", (int)val);
    if (fabs(val) < 370)
        return CPLSPrintf("%.16g", val);
    if (fabs(val) > 100000000.0)
        return CPLSPrintf("%.16g", val);

    return CPLSPrintf("%.3f", val);
}
#endif

static void AppendCoordinateList(OGRLineString *poLine, OGRILI1DataSource *poDS)
{
    const bool b3D = CPL_TO_BOOL(wkbHasZ(poLine->getGeometryType()));

    for (int iPoint = 0; iPoint < poLine->getNumPoints(); iPoint++)
    {
        if (iPoint == 0)
            VSIFPrintfL(poDS->GetTransferFile(), "STPT");
        else
            VSIFPrintfL(poDS->GetTransferFile(), "LIPT");
        VSIFPrintfL(poDS->GetTransferFile(), " %s",
                    d2str(poLine->getX(iPoint)));
        VSIFPrintfL(poDS->GetTransferFile(), " %s",
                    d2str(poLine->getY(iPoint)));
        if (b3D)
            VSIFPrintfL(poDS->GetTransferFile(), " %s",
                        d2str(poLine->getZ(iPoint)));
        VSIFPrintfL(poDS->GetTransferFile(), "\n");
    }
    VSIFPrintfL(poDS->GetTransferFile(), "ELIN\n");
}

static void AppendCompoundCurve(OGRCompoundCurve *poCC, OGRILI1DataSource *poDS)
{
    for (int iMember = 0; iMember < poCC->getNumCurves(); iMember++)
    {
        OGRCurve *poGeometry = poCC->getCurve(iMember);
        int b3D = wkbHasZ(poGeometry->getGeometryType());
        int bIsArc = (poGeometry->getGeometryType() == wkbCircularString ||
                      poGeometry->getGeometryType() == wkbCircularStringZ);
        OGRSimpleCurve *poLine = (OGRSimpleCurve *)poGeometry;
        for (int iPoint = 0; iPoint < poLine->getNumPoints(); iPoint++)
        {
            // Skip last point in curve member
            if (iPoint == poLine->getNumPoints() - 1 &&
                iMember < poCC->getNumCurves() - 1)
                continue;
            if (iMember == 0 && iPoint == 0)
                VSIFPrintfL(poDS->GetTransferFile(), "STPT");
            else if (bIsArc && iPoint == 1)
                VSIFPrintfL(poDS->GetTransferFile(), "ARCP");
            else
                VSIFPrintfL(poDS->GetTransferFile(), "LIPT");
            VSIFPrintfL(poDS->GetTransferFile(), " %s",
                        d2str(poLine->getX(iPoint)));
            VSIFPrintfL(poDS->GetTransferFile(), " %s",
                        d2str(poLine->getY(iPoint)));
            if (b3D)
                VSIFPrintfL(poDS->GetTransferFile(), " %s",
                            d2str(poLine->getZ(iPoint)));
            VSIFPrintfL(poDS->GetTransferFile(), "\n");
        }
    }
    VSIFPrintfL(poDS->GetTransferFile(), "ELIN\n");
}

int OGRILI1Layer::GeometryAppend(OGRGeometry *poGeometry)
{
#ifdef DEBUG_VERBOSE
    CPLDebug("OGR_ILI", "OGRILI1Layer::GeometryAppend OGRGeometryType: %s",
             OGRGeometryTypeToName(poGeometry->getGeometryType()));
#endif
    /* -------------------------------------------------------------------- */
    /*      2D Point                                                        */
    /* -------------------------------------------------------------------- */
    if (poGeometry->getGeometryType() == wkbPoint)
    {
        /* embedded in from non-geometry fields */
    }
    /* -------------------------------------------------------------------- */
    /*      3D Point                                                        */
    /* -------------------------------------------------------------------- */
    else if (poGeometry->getGeometryType() == wkbPoint25D)
    {
        /* embedded in from non-geometry fields */
    }

    /* -------------------------------------------------------------------- */
    /*      LineString and LinearRing                                       */
    /* -------------------------------------------------------------------- */
    else if (poGeometry->getGeometryType() == wkbLineString ||
             poGeometry->getGeometryType() == wkbLineString25D)
    {
        AppendCoordinateList(poGeometry->toLineString(), poDS);
    }

    /* -------------------------------------------------------------------- */
    /*      Polygon                                                         */
    /* -------------------------------------------------------------------- */
    else if (poGeometry->getGeometryType() == wkbPolygon ||
             poGeometry->getGeometryType() == wkbPolygon25D)
    {
        OGRPolygon *poPolygon = poGeometry->toPolygon();
        for (auto &&poRing : poPolygon)
        {
            if (!GeometryAppend(poRing))
                return FALSE;
        }
    }

    /* -------------------------------------------------------------------- */
    /*      MultiPolygon                                                    */
    /* -------------------------------------------------------------------- */
    else if (wkbFlatten(poGeometry->getGeometryType()) == wkbMultiPolygon ||
             wkbFlatten(poGeometry->getGeometryType()) == wkbMultiLineString ||
             wkbFlatten(poGeometry->getGeometryType()) == wkbMultiPoint ||
             wkbFlatten(poGeometry->getGeometryType()) ==
                 wkbGeometryCollection ||
             wkbFlatten(poGeometry->getGeometryType()) == wkbMultiCurve ||
             wkbFlatten(poGeometry->getGeometryType()) == wkbMultiCurveZ)
    {
        OGRGeometryCollection *poGC = poGeometry->toGeometryCollection();

#if 0
        // TODO: Why this large NOP block?
        if( wkbFlatten(poGeometry->getGeometryType()) == wkbMultiPolygon )
        {
        }
        else if( wkbFlatten(poGeometry->getGeometryType()) == wkbMultiLineString )
        {
        }
        else if( wkbFlatten(poGeometry->getGeometryType()) == wkbMultiPoint )
        {
        }
        else
        {
        }
#endif

        for (auto &&poMember : poGC)
        {
            if (!GeometryAppend(poMember))
                return FALSE;
        }
    }
    else if (poGeometry->getGeometryType() == wkbCompoundCurve ||
             poGeometry->getGeometryType() == wkbCompoundCurveZ)
    {
        AppendCompoundCurve(poGeometry->toCompoundCurve(), poDS);
    }
    else
    {
        CPLError(CE_Warning, CPLE_AppDefined,
                 "Skipping unknown geometry type '%s'",
                 OGRGeometryTypeToName(poGeometry->getGeometryType()));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/
/*                           ICreateFeature()                            */
/************************************************************************/

OGRErr OGRILI1Layer::ICreateFeature(OGRFeature *poFeature)
{
    // FIXME: tid not thread safe
    static long tid = -1;  // system generated TID (must be unique within table)
    VSILFILE *fp = poDS->GetTransferFile();
    VSIFPrintfL(fp, "OBJE");

    if (poFeatureDefn->GetFieldCount() &&
        !EQUAL(poFeatureDefn->GetFieldDefn(0)->GetNameRef(), "TID"))
    {
        // Input is not generated from an Interlis 1 source
        if (poFeature->GetFID() != OGRNullFID)
            tid = (int)poFeature->GetFID();
        else
            ++tid;
        VSIFPrintfL(fp, " %ld", tid);
        // Embedded geometry
        if (poFeature->GetGeometryRef() != nullptr)
        {
            OGRGeometry *poGeometry = poFeature->GetGeometryRef();
            // 2D Point
            if (poGeometry->getGeometryType() == wkbPoint)
            {
                OGRPoint *poPoint = poGeometry->toPoint();

                VSIFPrintfL(fp, " %s", d2str(poPoint->getX()));
                VSIFPrintfL(fp, " %s", d2str(poPoint->getY()));
            }
            // 3D Point
            else if (poGeometry->getGeometryType() == wkbPoint25D)
            {
                OGRPoint *poPoint = poGeometry->toPoint();

                VSIFPrintfL(fp, " %s", d2str(poPoint->getX()));
                VSIFPrintfL(fp, " %s", d2str(poPoint->getY()));
                VSIFPrintfL(fp, " %s", d2str(poPoint->getZ()));
            }
        }
    }

    // Write all fields.
    for (int iField = 0; iField < poFeatureDefn->GetFieldCount(); iField++)
    {
        if (poFeature->IsFieldSetAndNotNull(iField))
        {
            const char *pszRaw = poFeature->GetFieldAsString(iField);
            if (poFeatureDefn->GetFieldDefn(iField)->GetType() == OFTString)
            {
                // Interlis 1 encoding is ISO 8859-1 (Latin1) -> Recode from
                // UTF-8
                char *pszString =
                    CPLRecode(pszRaw, CPL_ENC_UTF8, CPL_ENC_ISO8859_1);
                // Replace spaces
                for (size_t i = 0; pszString[i] != '\0'; i++)
                {
                    if (pszString[i] == ' ')
                        pszString[i] = '_';
                }
                VSIFPrintfL(fp, " %s", pszString);
                CPLFree(pszString);
            }
            else
            {
                VSIFPrintfL(fp, " %s", pszRaw);
            }
        }
        else
        {
            VSIFPrintfL(fp, " @");
        }
    }
    VSIFPrintfL(fp, "\n");

    // Write out Geometry
    if (poFeature->GetGeometryRef() != nullptr)
    {
        GeometryAppend(poFeature->GetGeometryRef());
    }

    return OGRERR_NONE;
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRILI1Layer::TestCapability(CPL_UNUSED const char *pszCap)
{
    if (EQUAL(pszCap, OLCCurveGeometries))
        return TRUE;
    if (EQUAL(pszCap, OLCZGeometries))
        return TRUE;

    if (EQUAL(pszCap, OLCCreateField) || EQUAL(pszCap, OLCSequentialWrite))
        return poDS->GetTransferFile() != nullptr;

    return FALSE;
}

/************************************************************************/
/*                            CreateField()                             */
/************************************************************************/

OGRErr OGRILI1Layer::CreateField(const OGRFieldDefn *poField,
                                 int /* bApproxOK */)
{
    poFeatureDefn->AddFieldDefn(poField);

    return OGRERR_NONE;
}

/************************************************************************/
/*                         Internal routines                            */
/************************************************************************/

void OGRILI1Layer::JoinGeomLayers()
{
    bGeomsJoined = true;

    CPLConfigOptionSetter oSetter("OGR_ARC_STEPSIZE", "0.96",
                                  /* bSetOnlyIfUndefined = */ true);

    for (GeomFieldInfos::const_iterator it = oGeomFieldInfos.begin();
         it != oGeomFieldInfos.end(); ++it)
    {
        OGRFeatureDefn *geomFeatureDefn = it->second.GetGeomTableDefnRef();
        if (geomFeatureDefn)
        {
            CPLDebug("OGR_ILI", "Join geometry table %s of field '%s'",
                     geomFeatureDefn->GetName(), it->first.c_str());
            OGRILI1Layer *poGeomLayer =
                poDS->GetLayerByName(geomFeatureDefn->GetName());
            const int nGeomFieldIndex =
                GetLayerDefn()->GetGeomFieldIndex(it->first.c_str());
            if (it->second.iliGeomType == "Surface")
            {
                JoinSurfaceLayer(poGeomLayer, nGeomFieldIndex);
            }
            else if (it->second.iliGeomType == "Area")
            {
                CPLString pointField = it->first + "__Point";
                const int nPointFieldIndex =
                    GetLayerDefn()->GetGeomFieldIndex(pointField.c_str());
                PolygonizeAreaLayer(poGeomLayer, nGeomFieldIndex,
                                    nPointFieldIndex);
            }
        }
    }
}

void OGRILI1Layer::JoinSurfaceLayer(OGRILI1Layer *poSurfaceLineLayer,
                                    int nSurfaceFieldIndex)
{
    CPLDebug("OGR_ILI", "Joining surface layer %s with geometries",
             GetLayerDefn()->GetName());
    OGRwkbGeometryType geomType =
        GetLayerDefn()->GetGeomFieldDefn(nSurfaceFieldIndex)->GetType();

    std::map<OGRFeature *, std::vector<OGRCurve *>> oMapFeatureToGeomSet;

    poSurfaceLineLayer->ResetReading();

    // First map: for each target curvepolygon, find all belonging curves
    while (OGRFeature *linefeature = poSurfaceLineLayer->GetNextFeatureRef())
    {
        // OBJE entries with same _RefTID are polygon rings of same feature
        OGRFeature *feature = nullptr;
        if (poFeatureDefn->GetFieldDefn(0)->GetType() == OFTString)
        {
            feature = GetFeatureRef(linefeature->GetFieldAsString(1));
        }
        else
        {
            GIntBig reftid = linefeature->GetFieldAsInteger64(1);
            feature = GetFeatureRef(reftid);
        }
        if (feature)
        {
            OGRGeometry *poGeom = linefeature->GetGeomFieldRef(0);
            OGRMultiCurve *curves = dynamic_cast<OGRMultiCurve *>(poGeom);
            if (curves)
            {
                for (auto &&curve : curves)
                {
                    if (!curve->IsEmpty())
                        oMapFeatureToGeomSet[feature].push_back(curve);
                }
            }
        }
        else
        {
            CPLError(CE_Warning, CPLE_AppDefined,
                     "Couldn't join feature FID " CPL_FRMT_GIB,
                     linefeature->GetFieldAsInteger64(1));
        }
    }

    // Now for each target polygon, assemble the curves together.
    std::map<OGRFeature *, std::vector<OGRCurve *>>::const_iterator oIter =
        oMapFeatureToGeomSet.begin();
    for (; oIter != oMapFeatureToGeomSet.end(); ++oIter)
    {
        OGRFeature *feature = oIter->first;
        std::vector<OGRCurve *> oCurves = oIter->second;

        std::vector<OGRCurve *> oSetDestCurves;
        double dfLargestArea = 0.0;
        OGRCurve *poLargestCurve = nullptr;
        while (true)
        {
            std::vector<OGRCurve *>::iterator oIterCurves = oCurves.begin();
            if (oIterCurves == oCurves.end())
                break;

            OGRPoint endPointCC;
            OGRCompoundCurve *poCC = new OGRCompoundCurve();

            bool bFirst = true;
            while (true)
            {
                bool bNewCurveAdded = false;
                const double dfEps = 1e-14;
                for (oIterCurves = oCurves.begin();
                     oIterCurves != oCurves.end(); ++oIterCurves)
                {
                    OGRCurve *curve = *oIterCurves;
                    OGRPoint startPoint;
                    OGRPoint endPoint;
                    curve->StartPoint(&startPoint);
                    curve->EndPoint(&endPoint);
                    if (bFirst ||
                        (fabs(startPoint.getX() - endPointCC.getX()) < dfEps &&
                         fabs(startPoint.getY() - endPointCC.getY()) < dfEps))
                    {
                        bFirst = false;

                        curve->EndPoint(&endPointCC);

                        const OGRwkbGeometryType eCurveType =
                            wkbFlatten(curve->getGeometryType());
                        if (eCurveType == wkbCompoundCurve)
                        {
                            OGRCompoundCurve *poCCSub =
                                curve->toCompoundCurve();
                            for (auto &&subCurve : poCCSub)
                            {
                                poCC->addCurve(subCurve);
                            }
                        }
                        else
                        {
                            poCC->addCurve(curve);
                        }
                        oCurves.erase(oIterCurves);
                        bNewCurveAdded = true;
                        break;
                    }
                    else if (fabs(endPoint.getX() - endPointCC.getX()) <
                                 dfEps &&
                             fabs(endPoint.getY() - endPointCC.getY()) < dfEps)
                    {
                        curve->StartPoint(&endPointCC);

                        const OGRwkbGeometryType eCurveType =
                            wkbFlatten(curve->getGeometryType());
                        if (eCurveType == wkbLineString ||
                            eCurveType == wkbCircularString)
                        {
                            OGRSimpleCurve *poSC =
                                curve->toSimpleCurve()->clone();
                            poSC->reversePoints();
                            poCC->addCurveDirectly(poSC);
                        }
                        else if (eCurveType == wkbCompoundCurve)
                        {
                            // Reverse the order of the elements of the
                            // compound curve
                            OGRCompoundCurve *poCCSub =
                                curve->toCompoundCurve();
                            for (int i = poCCSub->getNumCurves() - 1; i >= 0;
                                 --i)
                            {
                                OGRSimpleCurve *poSC = poCCSub->getCurve(i)
                                                           ->toSimpleCurve()
                                                           ->clone();
                                poSC->reversePoints();
                                poCC->addCurveDirectly(poSC);
                            }
                        }

                        oCurves.erase(oIterCurves);
                        bNewCurveAdded = true;
                        break;
                    }
                }
                if (!bNewCurveAdded || oCurves.empty() || poCC->get_IsClosed())
                    break;
            }

            if (!poCC->get_IsClosed())
            {
                char *pszJSon = poCC->exportToJson();
                CPLError(CE_Warning, CPLE_AppDefined,
                         "A ring %s for feature " CPL_FRMT_GIB " in layer %s "
                         "was not closed. Dropping it",
                         pszJSon, feature->GetFID(), GetName());
                delete poCC;
                CPLFree(pszJSon);
            }
            else
            {
                double dfArea = poCC->get_Area();
                if (dfArea >= dfLargestArea)
                {
                    dfLargestArea = dfArea;
                    poLargestCurve = poCC;
                }
                oSetDestCurves.push_back(poCC);
            }
        }

        // Now build the final polygon by first inserting the largest ring.
        OGRCurvePolygon *poPoly =
            (geomType == wkbPolygon) ? new OGRPolygon() : new OGRCurvePolygon();
        if (poLargestCurve)
        {
            std::vector<OGRCurve *>::iterator oIterCurves =
                oSetDestCurves.begin();
            for (; oIterCurves != oSetDestCurves.end(); ++oIterCurves)
            {
                OGRCurve *poCurve = *oIterCurves;
                if (poCurve == poLargestCurve)
                {
                    oSetDestCurves.erase(oIterCurves);
                    break;
                }
            }

            if (geomType == wkbPolygon)
            {
                poLargestCurve = OGRCurve::CastToLinearRing(poLargestCurve);
            }
            OGRErr error = poPoly->addRingDirectly(poLargestCurve);
            if (error != OGRERR_NONE)
            {
                char *pszJSon = poLargestCurve->exportToJson();
                CPLError(CE_Warning, CPLE_AppDefined,
                         "Cannot add ring %s to feature " CPL_FRMT_GIB
                         " in layer %s",
                         pszJSon, feature->GetFID(), GetName());
                CPLFree(pszJSon);
            }

            oIterCurves = oSetDestCurves.begin();
            for (; oIterCurves != oSetDestCurves.end(); ++oIterCurves)
            {
                OGRCurve *poCurve = *oIterCurves;
                if (geomType == wkbPolygon)
                {
                    poCurve = OGRCurve::CastToLinearRing(poCurve);
                }
                error = poPoly->addRingDirectly(poCurve);
                if (error != OGRERR_NONE)
                {
                    char *pszJSon = poCurve->exportToJson();
                    CPLError(CE_Warning, CPLE_AppDefined,
                             "Cannot add ring %s to feature " CPL_FRMT_GIB
                             " in layer %s",
                             pszJSon, feature->GetFID(), GetName());
                    CPLFree(pszJSon);
                }
            }
        }

        feature->SetGeomFieldDirectly(nSurfaceFieldIndex, poPoly);
    }

    ResetReading();
}

OGRMultiPolygon *OGRILI1Layer::Polygonize(OGRGeometryCollection *poLines,
                                          bool
#if defined(HAVE_GEOS)
                                              fix_crossing_lines
#endif
)
{
    if (poLines->getNumGeometries() == 0)
    {
        return new OGRMultiPolygon();
    }

#if defined(HAVE_GEOS)
    GEOSGeom *ahInGeoms = nullptr;
    OGRGeometryCollection *poNoncrossingLines = poLines;
    GEOSGeom hResultGeom = nullptr;
    OGRGeometry *poMP = nullptr;

    if (fix_crossing_lines && poLines->getNumGeometries() > 0)
    {
        CPLDebug("OGR_ILI", "Fixing crossing lines");
        // A union of the geometry collection with one line fixes
        // invalid geometries.
        OGRGeometry *poUnion = poLines->Union(poLines->getGeometryRef(0));
        if (poUnion != nullptr)
        {
            if (wkbFlatten(poUnion->getGeometryType()) ==
                    wkbGeometryCollection ||
                wkbFlatten(poUnion->getGeometryType()) == wkbMultiLineString)
            {
                poNoncrossingLines =
                    dynamic_cast<OGRGeometryCollection *>(poUnion);
                CPLDebug("OGR_ILI", "Fixed lines: %d",
                         poNoncrossingLines->getNumGeometries() -
                             poLines->getNumGeometries());
            }
            else
            {
                delete poUnion;
            }
        }
    }

    GEOSContextHandle_t hGEOSCtxt = OGRGeometry::createGEOSContext();

    ahInGeoms = static_cast<GEOSGeom *>(
        CPLCalloc(sizeof(void *), poNoncrossingLines->getNumGeometries()));
    for (int i = 0; i < poNoncrossingLines->getNumGeometries(); i++)
        ahInGeoms[i] =
            poNoncrossingLines->getGeometryRef(i)->exportToGEOS(hGEOSCtxt);

    hResultGeom = GEOSPolygonize_r(hGEOSCtxt, ahInGeoms,
                                   poNoncrossingLines->getNumGeometries());

    for (int i = 0; i < poNoncrossingLines->getNumGeometries(); i++)
        GEOSGeom_destroy_r(hGEOSCtxt, ahInGeoms[i]);
    CPLFree(ahInGeoms);
    if (poNoncrossingLines != poLines)
        delete poNoncrossingLines;

    if (hResultGeom == nullptr)
    {
        OGRGeometry::freeGEOSContext(hGEOSCtxt);
        return new OGRMultiPolygon();
    }

    poMP = OGRGeometryFactory::createFromGEOS(hGEOSCtxt, hResultGeom);

    GEOSGeom_destroy_r(hGEOSCtxt, hResultGeom);
    OGRGeometry::freeGEOSContext(hGEOSCtxt);

    poMP = OGRGeometryFactory::forceToMultiPolygon(poMP);
    if (poMP && wkbFlatten(poMP->getGeometryType()) == wkbMultiPolygon)
        return dynamic_cast<OGRMultiPolygon *>(poMP);

    delete poMP;
    return new OGRMultiPolygon();

#else
    return new OGRMultiPolygon();
#endif
}

void OGRILI1Layer::PolygonizeAreaLayer(OGRILI1Layer *poAreaLineLayer,
                                       int
#if defined(HAVE_GEOS)
                                           nAreaFieldIndex
#endif
                                       ,
                                       int
#if defined(HAVE_GEOS)
                                           nPointFieldIndex
#endif
)
{
    // add all lines from poAreaLineLayer to collection
    OGRGeometryCollection *gc = new OGRGeometryCollection();
    poAreaLineLayer->ResetReading();
    while (OGRFeature *feature = poAreaLineLayer->GetNextFeatureRef())
        gc->addGeometry(feature->GetGeometryRef());

    // polygonize lines
    CPLDebug("OGR_ILI", "Polygonizing layer %s with %d multilines",
             poAreaLineLayer->GetLayerDefn()->GetName(),
             gc->getNumGeometries());
    poAreaLineLayer = nullptr;
    OGRMultiPolygon *polys = Polygonize(gc, false);
    CPLDebug("OGR_ILI", "Resulting polygons: %d", polys->getNumGeometries());
    if (polys->getNumGeometries() != GetFeatureCount())
    {
        CPLDebug("OGR_ILI", "Feature count of layer %s: " CPL_FRMT_GIB,
                 GetLayerDefn()->GetName(), GetFeatureCount());
        CPLDebug("OGR_ILI", "Polygonizing again with crossing line fix");
        delete polys;
        polys = Polygonize(gc, true);  // try again with crossing line fix
        CPLDebug("OGR_ILI", "Resulting polygons: %d",
                 polys->getNumGeometries());
    }
    delete gc;

    // associate polygon feature with data row according to centroid
#if defined(HAVE_GEOS)
    OGRPolygon emptyPoly;

    CPLDebug("OGR_ILI", "Associating layer %s with area polygons",
             GetLayerDefn()->GetName());
    GEOSGeom *ahInGeoms = static_cast<GEOSGeom *>(
        CPLCalloc(sizeof(void *), polys->getNumGeometries()));
    GEOSContextHandle_t hGEOSCtxt = OGRGeometry::createGEOSContext();
    for (int i = 0; i < polys->getNumGeometries(); i++)
    {
        ahInGeoms[i] = polys->getGeometryRef(i)->exportToGEOS(hGEOSCtxt);
        if (!GEOSisValid_r(hGEOSCtxt, ahInGeoms[i]))
            ahInGeoms[i] = nullptr;
    }
    for (int nFidx = 0; nFidx < nFeatures; nFidx++)
    {
        OGRFeature *feature = papoFeatures[nFidx];
        OGRGeometry *geomRef = feature->GetGeomFieldRef(nPointFieldIndex);
        if (!geomRef)
        {
            continue;
        }
        GEOSGeom point =
            reinterpret_cast<GEOSGeom>(geomRef->exportToGEOS(hGEOSCtxt));

        int i = 0;
        for (; i < polys->getNumGeometries(); i++)
        {
            if (ahInGeoms[i] && GEOSWithin_r(hGEOSCtxt, point, ahInGeoms[i]))
            {
                feature->SetGeomField(nAreaFieldIndex,
                                      polys->getGeometryRef(i));
                break;
            }
        }
        if (i == polys->getNumGeometries())
        {
            CPLDebug("OGR_ILI", "Association between area and point failed.");
            feature->SetGeometry(&emptyPoly);
        }
        GEOSGeom_destroy_r(hGEOSCtxt, point);
    }
    for (int i = 0; i < polys->getNumGeometries(); i++)
        GEOSGeom_destroy_r(hGEOSCtxt, ahInGeoms[i]);
    CPLFree(ahInGeoms);
    OGRGeometry::freeGEOSContext(hGEOSCtxt);
#endif
    delete polys;
}

/************************************************************************/
/*                             GetDataset()                             */
/************************************************************************/

GDALDataset *OGRILI1Layer::GetDataset()
{
    return poDS;
}
