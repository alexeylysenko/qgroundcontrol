/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


#include <QStringList>
#include <QDebug>

#include "VisualMissionItem.h"
#include "FirmwarePluginManager.h"
#include "QGCApplication.h"
#include "JsonHelper.h"
#include "TerrainQuery.h"
#include "TakeoffMissionItem.h"
#include "PlanMasterController.h"

const char* VisualMissionItem::jsonTypeKey =                "type";
const char* VisualMissionItem::jsonTypeSimpleItemValue =    "SimpleItem";
const char* VisualMissionItem::jsonTypeComplexItemValue =   "ComplexItem";

VisualMissionItem::VisualMissionItem(PlanMasterController* masterController, bool flyView, QObject* parent)
    : QObject           (parent)
    , _flyView          (flyView)
    , _masterController (masterController)
    , _controllerVehicle(masterController->controllerVehicle())
{
    _commonInit();
}

VisualMissionItem::VisualMissionItem(const VisualMissionItem& other, bool flyView, QObject* parent)
    : QObject                   (parent)
    , _flyView                  (flyView)
{
    *this = other;

    _commonInit();
}

void VisualMissionItem::_commonInit(void)
{
    // Don't get terrain altitude information for submarines or boats
    Vehicle* controllerVehicle = _masterController->controllerVehicle();
    if (controllerVehicle->vehicleType() != MAV_TYPE_SUBMARINE && controllerVehicle->vehicleType() != MAV_TYPE_SURFACE_BOAT) {
        _updateTerrainTimer.setInterval(500);
        _updateTerrainTimer.setSingleShot(true);
        connect(&_updateTerrainTimer, &QTimer::timeout, this, &VisualMissionItem::_reallyUpdateTerrainAltitude);

        connect(this, &VisualMissionItem::coordinateChanged, this, &VisualMissionItem::_updateTerrainAltitude);
    }
}

const VisualMissionItem& VisualMissionItem::operator=(const VisualMissionItem& other)
{
    _masterController = other._masterController;
    _controllerVehicle = other._controllerVehicle;

    setIsCurrentItem(other._isCurrentItem);
    setDirty(other._dirty);
    _homePositionSpecialCase = other._homePositionSpecialCase;
    _terrainAltitude = other._terrainAltitude;
    setAltDifference(other._altDifference);
    setAltPercent(other._altPercent);
    setTerrainPercent(other._terrainPercent);
    setAzimuth(other._azimuth);
    setDistance(other._distance);

    return *this;
}

VisualMissionItem::~VisualMissionItem()
{    
}

void VisualMissionItem::setIsCurrentItem(bool isCurrentItem)
{
    if (_isCurrentItem != isCurrentItem) {
        _isCurrentItem = isCurrentItem;
        emit isCurrentItemChanged(isCurrentItem);
    }
}

void VisualMissionItem::setHasCurrentChildItem(bool hasCurrentChildItem)
{
    if (_hasCurrentChildItem != hasCurrentChildItem) {
        _hasCurrentChildItem = hasCurrentChildItem;
        emit hasCurrentChildItemChanged(hasCurrentChildItem);
    }
}

void VisualMissionItem::setDistance(double distance)
{
    if (!qFuzzyCompare(_distance, distance)) {
        _distance = distance;
        emit distanceChanged(_distance);
    }
}

void VisualMissionItem::setAltDifference(double altDifference)
{
    if (!qFuzzyCompare(_altDifference, altDifference)) {
        _altDifference = altDifference;
        emit altDifferenceChanged(_altDifference);
    }
}

void VisualMissionItem::setAltPercent(double altPercent)
{
    if (!qFuzzyCompare(_altPercent, altPercent)) {
        _altPercent = altPercent;
        emit altPercentChanged(_altPercent);
    }
}

void VisualMissionItem::setTerrainPercent(double terrainPercent)
{
    if (!qFuzzyCompare(_terrainPercent, terrainPercent)) {
        _terrainPercent = terrainPercent;
        emit terrainPercentChanged(terrainPercent);
    }
}

void VisualMissionItem::setTerrainCollision(bool terrainCollision)
{
    if (terrainCollision != _terrainCollision) {
        _terrainCollision = terrainCollision;
        emit terrainCollisionChanged(terrainCollision);
    }
}

void VisualMissionItem::setAzimuth(double azimuth)
{
    if (!qFuzzyCompare(_azimuth, azimuth)) {
        _azimuth = azimuth;
        emit azimuthChanged(_azimuth);
    }
}

void VisualMissionItem::setMissionFlightStatus(MissionController::MissionFlightStatus_t& missionFlightStatus)
{
    _missionFlightStatus = missionFlightStatus;
    if (qIsNaN(_missionFlightStatus.gimbalYaw) && qIsNaN(_missionGimbalYaw)) {
        return;
    }
    if (_missionFlightStatus.gimbalYaw != _missionGimbalYaw) {
        _missionGimbalYaw = _missionFlightStatus.gimbalYaw;
        emit missionGimbalYawChanged(_missionGimbalYaw);
    }
}

void VisualMissionItem::setMissionVehicleYaw(double vehicleYaw)
{
    if (!qFuzzyCompare(_missionVehicleYaw, vehicleYaw)) {
        _missionVehicleYaw = vehicleYaw;
        emit missionVehicleYawChanged(_missionVehicleYaw);
    }
}

void VisualMissionItem::_updateTerrainAltitude(void)
{
    if (!coordinate().isValid()) {
        // This is an intermediate state we don't react to
        return;
    }
    if (!_flyView && specifiesCoordinate() && coordinate().isValid()) {
        // We use a timer so that any additional requests before the timer fires result in only a single request
        _updateTerrainTimer.start();
    } else {
        _terrainAltitude = qQNaN();
    }
}

void VisualMissionItem::_reallyUpdateTerrainAltitude(void)
{
    QGeoCoordinate coord = coordinate();
    if (specifiesCoordinate() && coord.isValid() && (qIsNaN(_terrainAltitude) || !qFuzzyCompare(_lastLatTerrainQuery, coord.latitude()) || qFuzzyCompare(_lastLonTerrainQuery, coord.longitude()))) {
        _lastLatTerrainQuery = coord.latitude();
        _lastLonTerrainQuery = coord.longitude();
        TerrainAtCoordinateQuery* terrain = new TerrainAtCoordinateQuery(this);
        connect(terrain, &TerrainAtCoordinateQuery::terrainDataReceived, this, &VisualMissionItem::_terrainDataReceived);
        QList<QGeoCoordinate> rgCoord;
        rgCoord.append(coordinate());
        terrain->requestData(rgCoord);
    }
}

void VisualMissionItem::_terrainDataReceived(bool success, QList<double> heights)
{
    _terrainAltitude = success ? heights[0] : qQNaN();
    emit terrainAltitudeChanged(_terrainAltitude);
    sender()->deleteLater();
}

void VisualMissionItem::_setBoundingCube(QGCGeoBoundingCube bc)
{
    if (bc != _boundingCube) {
        _boundingCube = bc;
        emit boundingCubeChanged();
    }
}

void VisualMissionItem::setWizardMode(bool wizardMode)
{
    if (wizardMode != _wizardMode) {
        _wizardMode = wizardMode;
        emit wizardModeChanged(_wizardMode);
    }
}

void VisualMissionItem::setParentItem(VisualMissionItem* parentItem)
{
    if (_parentItem != parentItem) {
        _parentItem = parentItem;
        emit parentItemChanged(parentItem);
    }
}
