require("ReferenceLatLongAlt")

lastPositionX = 0
lastPositionY = 0
lastPositionZ = 0

function convertUnrealPositionToDIS()
-- Since we are working over a fairly small part of the planet, we can assume a flat surface
--convert lat/long to geocentric
DSimLocal.X, DSimLocal.Y, DSimLocal.Z = EnuToEcef(DSimLocal.Y, DSimLocal.X, DSimLocal.Z, referenceOffset_Lat , referenceOffset_Long , referenceOffset_Alt )

lastPositionX = DSimLocal.X
lastPositionY = DSimLocal.Y
lastPositionZ = DSimLocal.Z

end