#include "SurfaceModelingTool.h"
#include "GeomFill_BSplineCurves.hxx"
#include "BSplCLib.hxx"
#include "Geom_BSplineCurve.hxx"
#include "Geom_BSplineSurface.hxx"
#include <GeomAPI_ExtremaCurveCurve.hxx>
#include <GeomAPI_ProjectPointOnCurve.hxx>
#include <algorithm>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_IntCS.hxx>
#include <TopExp_Explorer.hxx>
#include <GeomAPI_Interpolate.hxx>
#include "GeomAPI_ExtremaCurveCurve.hxx"
#include <GeomAPI_PointsToBSpline.hxx>
#include <GeomAdaptor_Curve.hxx>
#include "TColgp_HArray1OfPnt.hxx"
#include "Geom_TrimmedCurve.hxx"
#include <BRepTools.hxx>
#include "BRep_Builder.hxx"
#include <GeomConvert.hxx>
#include "Geom_Line.hxx"
#include <STEPControl_Reader.hxx>
//3rd part
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <Eigen/Dense>
#include <map>
#include <cmath>
#include <memory>
#include <algorithm>
#include <functional>
#include <numeric> 
#include <IGESControl_Reader.hxx>
#include <KnotUpdate.h>
#include <RealCompare.h>
#include <GeomAPI_ProjectPointOnSurf.hxx>

#include <filesystem>
//=======================================================================
//function : SetSameDistribution
//purpose  : Internal Use Only
//=======================================================================

Standard_Integer SurfaceModelingTool::SetSameDistribution(Handle(
	Geom_BSplineCurve)& C1,
	Handle(Geom_BSplineCurve)& C2) 
{
	Standard_Integer C1_Degree = C1->Degree();
	Standard_Integer C2_Degree = C2->Degree();
	// 如果Degree不相同的话，对低阶进行升阶
	if (C1_Degree < C2_Degree) 
	{
		C1->IncreaseDegree(C2_Degree);
	}
	else
	{
		C2->IncreaseDegree(C1_Degree);
	}
	Standard_Integer nbp1 = C1->NbPoles();
	Standard_Integer nbk1 = C1->NbKnots();
	TColgp_Array1OfPnt      P1(1, nbp1);
	TColStd_Array1OfReal    W1(1, nbp1);
	W1.Init(1.);
	TColStd_Array1OfReal    K1(1, nbk1);
	TColStd_Array1OfInteger M1(1, nbk1);

	C1->Poles(P1);
	if (C1->IsRational())
		C1->Weights(W1);
	C1->Knots(K1);
	C1->Multiplicities(M1);

	Standard_Integer nbp2 = C2->NbPoles();
	Standard_Integer nbk2 = C2->NbKnots();
	TColgp_Array1OfPnt      P2(1, nbp2);
	TColStd_Array1OfReal    W2(1, nbp2);
	W2.Init(1.);
	TColStd_Array1OfReal    K2(1, nbk2);
	TColStd_Array1OfInteger M2(1, nbk2);

	C2->Poles(P2);
	if (C2->IsRational())
		C2->Weights(W2);
	C2->Knots(K2);
	C2->Multiplicities(M2);

	Standard_Real K11 = K1(1);
	Standard_Real K12 = K1(nbk1);
	Standard_Real K21 = K2(1);
	Standard_Real K22 = K2(nbk2);

	if ((K12 - K11) > (K22 - K21)) {
		BSplCLib::Reparametrize(K11, K12, K2);
		C2->SetKnots(K2);
	}
	else if ((K12 - K11) < (K22 - K21)) {
		BSplCLib::Reparametrize(K21, K22, K1);
		C1->SetKnots(K1);
	}
	else if (Abs(K12 - K11) > Precision::PConfusion()) {
		BSplCLib::Reparametrize(K11, K12, K2);
		C2->SetKnots(K2);
	}

	Standard_Integer NP, NK;
	if (BSplCLib::PrepareInsertKnots(C1->Degree(), Standard_False,
		K1, M1, K2, &M2, NP, NK, Precision::PConfusion(),
		Standard_False)) {
		TColgp_Array1OfPnt      NewP(1, NP);
		TColStd_Array1OfReal    NewW(1, NP);
		TColStd_Array1OfReal    NewK(1, NK);
		TColStd_Array1OfInteger NewM(1, NK);
		BSplCLib::InsertKnots(C1->Degree(), Standard_False,
			P1, &W1, K1, M1, K2, &M2,
			NewP, &NewW, NewK, NewM, Precision::PConfusion(),
			Standard_False);
		if (C1->IsRational()) {
			C1 = new Geom_BSplineCurve(NewP, NewW, NewK, NewM, C1->Degree());
		}
		else {
			C1 = new Geom_BSplineCurve(NewP, NewK, NewM, C1->Degree());
		}
		BSplCLib::InsertKnots(C2->Degree(), Standard_False,
			P2, &W2, K2, M2, K1, &M1,
			NewP, &NewW, NewK, NewM, Precision::PConfusion(),
			Standard_False);
		if (C2->IsRational()) {
			C2 = new Geom_BSplineCurve(NewP, NewW, NewK, NewM, C2->Degree());
		}
		else {
			C2 = new Geom_BSplineCurve(NewP, NewK, NewM, C2->Degree());
		}
	}
	else {
		throw Standard_ConstructionError(" ");
	}

	return C1->NbPoles();
}

void SurfaceModelingTool::Coons_G0(Handle(Geom_BSplineCurve)& curve1, Handle(Geom_BSplineCurve)& curve2, Handle(Geom_BSplineCurve)& curve3, Handle(Geom_BSplineCurve)& curve4, Handle(Geom_BSplineSurface)& mySurface_coons)
{
	// First make the opposite boundary curves compatible,
	// Second generate the ruled surface in the u/v direction and of the four corner points
	// Third generate the sum surface 

	//1: Make Curve Compatible
	//curve 1 and curve 3 are opposite curves, make them compatible
	SetSameDistribution(curve1, curve3);
	//curve 2 and curve 4 are opposite curves, make them compatible
	SetSameDistribution(curve2, curve4);

	//2: Generate the coons Bspline surface

	//Get the control point number in the u/v direction
	Standard_Integer NbUPoles = curve1->NbPoles();
	Standard_Integer NbVPoles = curve2->NbPoles();

	//control points of the coons surface
	TColgp_Array2OfPnt Poles_result(1, NbUPoles, 1, NbVPoles);

	//control points of ruled surface in the v direction
	TColgp_Array2OfPnt Poles_vruled(1, NbUPoles, 1, 2);

	//control points of ruled surface in the u direction
	TColgp_Array2OfPnt Poles_uruled(1, 2, 1, NbVPoles);

	//control points of ruled surface of the four corner points
	TColgp_Array2OfPnt Poles_uruled_vruled(1, 2, 1, 2);

	// Get the u knot vector
	Standard_Integer NbUKnot = curve1->NbKnots();
	TColStd_Array1OfReal    UKnots(1, NbUKnot);
	TColStd_Array1OfInteger UMults(1, NbUKnot);
	curve1->Knots(UKnots);
	curve1->Multiplicities(UMults);

	// Get the v knot vector
	Standard_Integer NbVKnot = curve2->NbKnots();
	TColStd_Array1OfReal    VKnots(1, NbVKnot);
	TColStd_Array1OfInteger VMults(1, NbVKnot);
	curve2->Knots(VKnots);
	curve2->Multiplicities(VMults);

	// Set v knots of the v ruled surface
	TColStd_Array1OfReal    VKnots_RuledInVdirection(1, 2);
	TColStd_Array1OfInteger VMults_RuledInVdirection(1, 2);
	VKnots_RuledInVdirection(1) = curve2->FirstParameter();
	VKnots_RuledInVdirection(2) = curve2->LastParameter();

	VMults_RuledInVdirection(1) = 2;
	VMults_RuledInVdirection(2) = 2;

	// Set u knots of the u ruled surface
	TColStd_Array1OfReal    UKnots_RuledInUdirection(1, 2);
	TColStd_Array1OfInteger UMults_RuledInUdirection(1, 2);
	UKnots_RuledInUdirection(1) = curve1->FirstParameter();
	UKnots_RuledInUdirection(2) = curve1->LastParameter();

	UMults_RuledInUdirection(1) = 2;
	UMults_RuledInUdirection(2) = 2;

	// 
	//2.1 generate the ruled surface in the v direction
	// 
	// 
	// Set control points of the v ruled surface
	for (int i = 1; i <= NbUPoles; i++)
	{
		Poles_vruled(i, 1) = curve1->Pole(i);
		Poles_vruled(i, 2) = curve3->Pole(i);
	}

	// Generate the bspline surface
	Handle(Geom_BSplineSurface) mySurface_VRuled = new Geom_BSplineSurface(Poles_vruled,
		UKnots, VKnots_RuledInVdirection,
		UMults, VMults_RuledInVdirection,
		curve1->Degree(), 1);

	// Make the surface compatible with the result coons surface
	mySurface_VRuled->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbVKnot; i++)
		mySurface_VRuled->InsertVKnot(VKnots(i), VMults(i), Precision::Confusion());

	// 
	//2.2 generate the ruled surface in the u direction
	// 

	// Set control points of the u ruled surface
	for (int i = 1; i <= NbVPoles; i++)
	{
		Poles_uruled(1, i) = curve4->Pole(i);
		Poles_uruled(2, i) = curve2->Pole(i);
	}

	Handle(Geom_BSplineSurface) mySurface_URuled = new Geom_BSplineSurface(Poles_uruled,
		UKnots_RuledInUdirection, VKnots,
		UMults_RuledInUdirection, VMults,
		1, curve2->Degree());

	//increase degree
	mySurface_URuled->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbUKnot; i++)
		mySurface_URuled->InsertUKnot(UKnots(i), UMults(i), Precision::Confusion());

	// 
	//2.3 generate the ruled surface of the four corner points
	// 
	// 
	Poles_uruled_vruled(1, 1) = curve1->Pole(1);
	Poles_uruled_vruled(1, 2) = curve3->Pole(1);
	Poles_uruled_vruled(2, 1) = curve1->Pole(NbUPoles);
	Poles_uruled_vruled(2, 2) = curve3->Pole(NbUPoles);

	Handle(Geom_BSplineSurface) mySurface_RuledSurfaceof4CornerPoints = new Geom_BSplineSurface(Poles_uruled_vruled,
		UKnots_RuledInUdirection, VKnots_RuledInVdirection,
		UMults_RuledInUdirection, VMults_RuledInVdirection,
		1, 1);

	mySurface_RuledSurfaceof4CornerPoints->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbVKnot; i++)
		mySurface_RuledSurfaceof4CornerPoints->InsertVKnot(VKnots(i), VMults(i), Precision::Confusion());

	for (int i = 1; i <= NbUKnot; i++)
		mySurface_RuledSurfaceof4CornerPoints->InsertUKnot(UKnots(i), UMults(i), Precision::Confusion());

	//2.4 Generate the sum surface mySurface_VRuled + mySurface_URuled - mySurface_RuledSurfaceof4CornerPoints 
	//increase degree

	for (int i = 1; i <= NbUPoles; i++)
	{
		for (int j = 1; j <= NbVPoles; j++)
		{
			Poles_result(i, j).SetXYZ(mySurface_VRuled->Pole(i, j).XYZ() + mySurface_URuled->Pole(i, j).XYZ() - mySurface_RuledSurfaceof4CornerPoints->Pole(i, j).XYZ());
		}
	}

	mySurface_coons = new Geom_BSplineSurface(Poles_result,
		UKnots, VKnots,
		UMults, VMults,
		curve1->Degree(), curve2->Degree());
}

void SurfaceModelingTool::Coons_G1(Handle(Geom_BSplineCurve)& curve1, Handle(Geom_BSplineCurve)& curve2, Handle(Geom_BSplineCurve)& curve3, Handle(Geom_BSplineCurve)& curve4, Handle(Geom_BSplineCurve)& curve1_derivative, Handle(Geom_BSplineCurve)& curve2_derivative, Handle(Geom_BSplineCurve)& curve3_derivative, Handle(Geom_BSplineCurve)& curve4_derivative, Handle(Geom_BSplineSurface)& mySurface_coons)
{
	// First make the opposite boundary curves compatible,
	// Second generate the ruled surface in the u/v direction and of the four corner points
	// Third generate the sum surface 

	//
	//1.1 Make Curve and the Derivative Compatible
	SetSameDistribution(curve1, curve1_derivative);
	SetSameDistribution(curve2, curve2_derivative);
	SetSameDistribution(curve3, curve3_derivative);
	SetSameDistribution(curve4, curve4_derivative);

	//1.2: Make opposite Curves Compatible
	//curve 1 and curve 3 are opposite curves, make them compatible
	SetSameDistribution(curve1, curve3);
	//curve 2 and curve 4 are opposite curves, make them compatible
	SetSameDistribution(curve2, curve4);

	//1.3 distribute to the derivatives
	//     
	SetSameDistribution(curve1, curve1_derivative);
	SetSameDistribution(curve2, curve2_derivative);
	SetSameDistribution(curve3, curve3_derivative);
	SetSameDistribution(curve4, curve4_derivative);


	//2: Generate the coons Bspline surface

	//Get the control point number in the u/v direction
	Standard_Integer NbUPoles = curve1->NbPoles();
	Standard_Integer NbVPoles = curve2->NbPoles();

	//control points of the coons surface
	TColgp_Array2OfPnt Poles_result(1, NbUPoles, 1, NbVPoles);

	//control points of G1 surface in the v direction
	TColgp_Array2OfPnt Poles_v(1, NbUPoles, 1, 4);

	//control points of G1 surface in the u direction
	TColgp_Array2OfPnt Poles_u(1, 4, 1, NbVPoles);

	//control points of G1 surface of the four corner points
	TColgp_Array2OfPnt Poles_u_v(1, 4, 1, 4);

	// Get the u knot vector
	Standard_Integer NbUKnot = curve1->NbKnots();
	TColStd_Array1OfReal    UKnots(1, NbUKnot);
	TColStd_Array1OfInteger UMults(1, NbUKnot);
	curve1->Knots(UKnots);
	curve1->Multiplicities(UMults);

	// Get the v knot vector
	Standard_Integer NbVKnot = curve2->NbKnots();
	TColStd_Array1OfReal    VKnots(1, NbVKnot);
	TColStd_Array1OfInteger VMults(1, NbVKnot);
	curve2->Knots(VKnots);
	curve2->Multiplicities(VMults);

	// Set v knots of the v ruled surface
	TColStd_Array1OfReal    VKnots_G1InVdirection(1, 2);
	TColStd_Array1OfInteger VMults_G1InVdirection(1, 2);
	VKnots_G1InVdirection(1) = curve2->FirstParameter();
	VKnots_G1InVdirection(2) = curve2->LastParameter();

	VMults_G1InVdirection(1) = 4;
	VMults_G1InVdirection(2) = 4;

	// Set u knots of the u ruled surface
	TColStd_Array1OfReal    UKnots_G1InUdirection(1, 2);
	TColStd_Array1OfInteger UMults_G1InUdirection(1, 2);
	UKnots_G1InUdirection(1) = curve1->FirstParameter();
	UKnots_G1InUdirection(2) = curve1->LastParameter();

	UMults_G1InUdirection(1) = 4;
	UMults_G1InUdirection(2) = 4;

	// 
	//2.1 generate the ruled surface in the v direction
	// 
	// 
	gp_Pnt p1, p2, p1d, p2d;
	// Set control points of the v ruled surface
	for (int i = 1; i <= NbUPoles; i++)
	{
		p1 = curve1->Pole(i);
		p1d = curve1_derivative->Pole(i);
		p2 = curve3->Pole(i);
		p2d = curve3_derivative->Pole(i);

		Poles_v(i, 1) = p1;
		Poles_v(i, 2).SetXYZ(p1.XYZ() + p1d.XYZ() / 3);
		Poles_v(i, 3).SetXYZ(p2.XYZ() - p2d.XYZ() / 3);
		Poles_v(i, 4) = p2;
	}

	// Generate the bspline surface
	Handle(Geom_BSplineSurface) mySurface_V = new Geom_BSplineSurface(Poles_v,
		UKnots, VKnots_G1InVdirection,
		UMults, VMults_G1InVdirection,
		curve1->Degree(), 3);

	// Make the surface compatible with the result coons surface
	mySurface_V->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbVKnot; i++)
		mySurface_V->InsertVKnot(VKnots(i), VMults(i), Precision::Confusion());

	// 
	//2.2 generate the ruled surface in the u direction
	// 

	// Set control points of the u ruled surface
	for (int i = 1; i <= NbVPoles; i++)
	{
		p1 = curve4->Pole(i);
		p1d = curve4_derivative->Pole(i);
		p2 = curve2->Pole(i);
		p2d = curve2_derivative->Pole(i);

		Poles_u(1, i) = p1;
		Poles_u(2, i).SetXYZ(p1.XYZ() + p1d.XYZ() / 3);
		Poles_u(3, i).SetXYZ(p2.XYZ() - p2d.XYZ() / 3);
		Poles_u(4, i) = p2;
	}

	Handle(Geom_BSplineSurface) mySurface_U = new Geom_BSplineSurface(Poles_u,
		UKnots_G1InUdirection, VKnots,
		UMults_G1InUdirection, VMults,
		3, curve2->Degree());

	//increase degree
	mySurface_U->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbUKnot; i++)
		mySurface_U->InsertUKnot(UKnots(i), UMults(i), Precision::Confusion());

	// 
	//2.3 generate the ruled surface of the four corner points
	// 
	// 
	gp_Vec T00_1, T00_2, T10_1, T10_2, T01_1, T01_2, T11_1, T11_2;
	gp_Pnt point;
	curve1_derivative->D1(curve1_derivative->FirstParameter(), point, T00_1);
	curve4_derivative->D1(curve4_derivative->FirstParameter(), point, T00_2);
	//Twist Compatiblity: T00_1 should coicide with T00_2
	curve1_derivative->D1(curve1_derivative->LastParameter(), point, T10_1);
	curve2_derivative->D1(curve2_derivative->FirstParameter(), point, T10_2);

	curve3_derivative->D1(curve3_derivative->FirstParameter(), point, T01_1);
	curve4_derivative->D1(curve4_derivative->LastParameter(), point, T01_2);

	curve2_derivative->D1(curve2_derivative->LastParameter(), point, T11_1);
	curve3_derivative->D1(curve3_derivative->LastParameter(), point, T11_2);

	Poles_u_v(1, 1) = curve1->Pole(1);
	Poles_u_v(2, 1).SetXYZ(curve1->Pole(1).XYZ() + curve4_derivative->Pole(1).XYZ() / 3);
	Poles_u_v(3, 1).SetXYZ(curve1->Pole(NbUPoles).XYZ() - curve2_derivative->Pole(1).XYZ() / 3);
	Poles_u_v(4, 1) = curve1->Pole(NbUPoles);

	Poles_u_v(1, 2).SetXYZ(curve1->Pole(1).XYZ() + curve1_derivative->Pole(1).XYZ() / 3);
	//Poles_u_v(2, 2).SetXYZ(curve1->Pole(1).XYZ() + curve1_derivative->Pole(1).XYZ() / 3);;
	//Poles_u_v(3, 2) = curve3->Pole(1);
	Poles_u_v(4, 2).SetXYZ(curve1->Pole(NbUPoles).XYZ() + curve1_derivative->Pole(NbUPoles).XYZ() / 3);

	Poles_u_v(1, 3).SetXYZ(curve3->Pole(1).XYZ() - curve3_derivative->Pole(1).XYZ() / 3);
	//Poles_u_v(2, 3).SetXYZ(curve1->Pole(1).XYZ() + curve1_derivative->Pole(1).XYZ() / 3);;
	//Poles_u_v(3, 3) = curve3->Pole(1);
	Poles_u_v(4, 3).SetXYZ(curve3->Pole(NbUPoles).XYZ() - curve3_derivative->Pole(NbUPoles).XYZ() / 3);

	Poles_u_v(1, 4).SetXYZ(curve3->Pole(1).XYZ());
	Poles_u_v(2, 4).SetXYZ(curve3->Pole(1).XYZ() + curve4_derivative->Pole(NbVPoles).XYZ() / 3);
	Poles_u_v(3, 4).SetXYZ(curve3->Pole(NbUPoles).XYZ() - curve2_derivative->Pole(NbVPoles).XYZ() / 3);
	Poles_u_v(4, 4).SetXYZ(curve3->Pole(NbUPoles).XYZ());

	Poles_u_v(2, 2).SetXYZ(Poles_u_v(2, 1).XYZ() + Poles_u_v(1, 2).XYZ() - Poles_u_v(1, 1).XYZ() + (T00_1.XYZ() + T00_2.XYZ()) / 18);
	Poles_u_v(3, 2).SetXYZ(Poles_u_v(3, 1).XYZ() + Poles_u_v(4, 2).XYZ() - Poles_u_v(4, 1).XYZ() + (T10_1.XYZ() + T10_2.XYZ()) / 18);
	Poles_u_v(2, 3).SetXYZ(Poles_u_v(1, 3).XYZ() + Poles_u_v(2, 4).XYZ() - Poles_u_v(1, 4).XYZ() + (T01_1.XYZ() + T01_2.XYZ()) / 18);
	Poles_u_v(3, 3).SetXYZ(Poles_u_v(3, 4).XYZ() + Poles_u_v(4, 3).XYZ() - Poles_u_v(4, 4).XYZ() + (T11_1.XYZ() + T11_2.XYZ()) / 18);

	Handle(Geom_BSplineSurface) mySurface_G1Surfaceof4CornerPoints = new Geom_BSplineSurface(Poles_u_v,
		UKnots_G1InUdirection, VKnots_G1InVdirection,
		UMults_G1InUdirection, VMults_G1InVdirection,
		3, 3);

	mySurface_G1Surfaceof4CornerPoints->IncreaseDegree(curve1->Degree(), curve2->Degree());

	for (int i = 1; i <= NbVKnot; i++)
		mySurface_G1Surfaceof4CornerPoints->InsertVKnot(VKnots(i), VMults(i), Precision::Confusion());

	for (int i = 1; i <= NbUKnot; i++)
		mySurface_G1Surfaceof4CornerPoints->InsertUKnot(UKnots(i), UMults(i), Precision::Confusion());

	//2.4 Generate the sum surface mySurface_VRuled + mySurface_URuled - mySurface_RuledSurfaceof4CornerPoints 
	//increase degree

	for (int i = 1; i <= NbUPoles; i++)
	{
		for (int j = 1; j <= NbVPoles; j++)
		{
			Poles_result(i, j).SetXYZ(mySurface_V->Pole(i, j).XYZ() + mySurface_U->Pole(i, j).XYZ() - mySurface_G1Surfaceof4CornerPoints->Pole(i, j).XYZ());
		}
	}

	mySurface_coons = new Geom_BSplineSurface(Poles_result,
		UKnots, VKnots,
		UMults, VMults,
		curve1->Degree(), curve2->Degree());
}

int SurfaceModelingTool:: Arrange_Coons_G0(std::vector<Handle(Geom_BSplineCurve)>& curveArray, Handle(Geom_BSplineCurve)& bslpineCurve1, Handle(Geom_BSplineCurve)& bslpineCurve2, Handle(Geom_BSplineCurve)& bslpineCurve3, Handle(Geom_BSplineCurve)& bslpineCurve4, double Tol, int IsModify)
{
	//Standard_Real Tol = 2;
	//Currently only work for four curves
	std::vector<Handle(Geom_BSplineCurve)> curveArraybak(curveArray);

	if (curveArraybak.size() != 4)
		return 0;

	bslpineCurve1 = curveArraybak[0];
	bslpineCurve2 = curveArraybak[1];
	bslpineCurve3 = curveArraybak[2];
	bslpineCurve4 = curveArraybak[3];

	Standard_Real maxDis = -1;
	Standard_Real dis;
	gp_Pnt curve1startpoint = bslpineCurve1->StartPoint();
	gp_Pnt curve1endpoint = bslpineCurve1->EndPoint();

	curveArraybak.erase(curveArraybak.begin());
	gp_Pnt curve2startpoint = bslpineCurve2->StartPoint();
	gp_Pnt curve2endpoint = bslpineCurve2->EndPoint();

	gp_Pnt curve3startpoint = bslpineCurve3->StartPoint();
	gp_Pnt curve3endpoint = bslpineCurve3->EndPoint();

	gp_Pnt curve4startpoint = bslpineCurve4->StartPoint();
	gp_Pnt curve4endpoint = bslpineCurve4->EndPoint();

	gp_Pnt point;
	//fine curve2
	for (int i = 0; i < curveArraybak.size(); i++)
	{
		bslpineCurve2 = curveArraybak[i];

		curve2startpoint = bslpineCurve2->StartPoint();
		curve2endpoint = bslpineCurve2->EndPoint();

		if (curve1endpoint.IsEqual(curve2startpoint, Tol))
		{
			dis = curve1endpoint.Distance(curve2startpoint);
			if (dis > maxDis)
				maxDis = dis;

			if (IsModify && dis < Tol)
			{
				curve1endpoint.BaryCenter(0.5, curve2startpoint, 0.5);
				bslpineCurve2->SetPole(1, curve1endpoint);
				bslpineCurve1->SetPole(bslpineCurve1->NbPoles(), curve1endpoint);
			}

			curveArraybak.erase(curveArraybak.begin() + i);
			break;
		}
		if (curve1endpoint.IsEqual(curve2endpoint, Tol))
		{
			dis = curve1endpoint.Distance(curve2endpoint);
			if (dis > maxDis)
				maxDis = dis;

			if (IsModify && dis < Tol)
			{
				curve1endpoint.BaryCenter(0.5, curve2endpoint, 0.5);
				bslpineCurve2->SetPole(bslpineCurve2->NbPoles(), curve1endpoint);
				bslpineCurve1->SetPole(bslpineCurve1->NbPoles(), curve1endpoint);
			}

			curveArraybak.erase(curveArraybak.begin() + i);
			bslpineCurve2->Reverse();

			curve2startpoint = bslpineCurve2->StartPoint();
			curve2endpoint = bslpineCurve2->EndPoint();
			break;
		}
	}

	//fine curve3
	for (int i = 0; i < curveArraybak.size(); i++)
	{
		bslpineCurve3 = curveArraybak[i];

		curve3startpoint = bslpineCurve3->StartPoint();
		curve3endpoint = bslpineCurve3->EndPoint();

		if (curve2endpoint.IsEqual(curve3endpoint, Tol))
		{
			dis = curve2endpoint.Distance(curve3endpoint);
			if (dis > maxDis)
				maxDis = dis;

			if (IsModify && dis < Tol)
			{
				curve2endpoint.BaryCenter(0.5, curve3endpoint, 0.5);
				bslpineCurve2->SetPole(bslpineCurve2->NbPoles(), curve2endpoint);
				bslpineCurve3->SetPole(bslpineCurve3->NbPoles(), curve2endpoint);
			}

			curveArraybak.erase(curveArraybak.begin() + i);
			break;
		}
		if (curve2endpoint.IsEqual(curve3startpoint, Tol))
		{
			dis = curve2endpoint.Distance(curve3startpoint);
			if (dis > maxDis)
				maxDis = dis;

			if (IsModify && dis < Tol)
			{
				curve2endpoint.BaryCenter(0.5, curve3startpoint, 0.5);
				bslpineCurve2->SetPole(bslpineCurve2->NbPoles(), curve2endpoint);
				bslpineCurve3->SetPole(1, curve2endpoint);
			}

			curveArraybak.erase(curveArraybak.begin() + i);
			bslpineCurve3->Reverse();

			curve3startpoint = bslpineCurve3->StartPoint();
			curve3endpoint = bslpineCurve3->EndPoint();
			break;
		}
	}
	//

	if (curveArraybak.size() != 1)
		return 0;

	bslpineCurve4 = curveArraybak[0];
	curve4startpoint = bslpineCurve4->StartPoint();
	curve4endpoint = bslpineCurve4->EndPoint();
	if (curve3startpoint.IsEqual(curve4endpoint, Tol))
	{
		dis = curve3startpoint.Distance(curve4endpoint);
		if (dis > maxDis)
			maxDis = dis;

		if (IsModify && dis < Tol)
		{
			curve3startpoint.BaryCenter(0.5, curve4endpoint, 0.5);
			bslpineCurve3->SetPole(1, curve3startpoint);
			bslpineCurve4->SetPole(bslpineCurve4->NbPoles(), curve3startpoint);
		}
	}
	else
		if (curve3startpoint.IsEqual(curve4startpoint, Tol))
		{
			dis = curve3startpoint.Distance(curve4startpoint);
			if (dis > maxDis)
				maxDis = dis;

			if (IsModify && dis < Tol)
			{
				curve3startpoint.BaryCenter(0.5, curve4startpoint, 0.5);
				bslpineCurve3->SetPole(1, curve3startpoint);
				bslpineCurve4->SetPole(1, curve3startpoint);
			}

			bslpineCurve4->Reverse();
			curve4startpoint = bslpineCurve4->StartPoint();
			curve4endpoint = bslpineCurve4->EndPoint();
		}
		else
			return 0;
	return 1;
}


double ComputeCurveCurveDistance(const Handle(Geom_BSplineCurve)& curve, const
	Handle(Geom_BSplineCurve)& boundaryCurve)
{ // 使用曲线的几何表示
	GeomAPI_ExtremaCurveCurve extrema(curve, boundaryCurve);
	// 检查是否找到了极值点
	if (extrema.NbExtrema() > 0)
	{
		// 遍历所有极值点，找到最小距离
		double minDistance = RealLast();
		for (int i = 1; i <= extrema.NbExtrema(); ++i)
		{
			Standard_Real dist = extrema.Distance(i);
			if (dist < minDistance)
			{
				minDistance = dist;
			}
		}
		return minDistance;
	}
}
// 计算曲线的采样点平均坐标
gp_Pnt ComputeAverageSamplePoint(const Handle(Geom_BSplineCurve)& curve, int numSamples) {
	GeomAdaptor_Curve adaptor(curve);
	double startParam = adaptor.FirstParameter();
	double endParam = adaptor.LastParameter();
	double deltaParam = (endParam - startParam) / (numSamples - 1);
	double X = 0, Y = 0, Z = 0;
	for (int i = 0; i < numSamples; ++i) {
		double param = startParam + i * deltaParam;
		gp_Pnt sample = adaptor.Value(param);
		X += sample.X();
		Y += sample.Y();
		Z += sample.Z();
	}
	X /= numSamples;
	Y /= numSamples;
	Z /= numSamples;
	return gp_Pnt(X, Y, Z);
}

void SurfaceModelingTool::ClassifyAndSortISOcurves(const std::vector<Handle(Geom_BSplineCurve)>&
	anISOcurvesArray,
	std::vector<Handle(Geom_BSplineCurve)>& uISOcurvesArray,
	std::vector<Handle(Geom_BSplineCurve)>& vISOcurvesArray)
{
	if (anISOcurvesArray.empty())
	{
		return;
	}

	// 计算所有等参线的采样中点，即所有等参线的中心点
	double x = 0, y = 0, z = 0;
	for (const auto& curve : anISOcurvesArray)
	{
		gp_Pnt avgPnt = ComputeAverageSamplePoint(curve, 10);
		x += avgPnt.X() / anISOcurvesArray.size();
		y += avgPnt.Y() / anISOcurvesArray.size();
		z += avgPnt.Z() / anISOcurvesArray.size();
	}
	// 所有等参线的中心点，可以理解为曲面的中心
	gp_Pnt middlePoint(x, y, z);
	// 取出第一条曲线来确定U方向
	const Handle(Geom_BSplineCurve)& firstCurve = anISOcurvesArray.front();
	uISOcurvesArray.push_back(firstCurve);
	for (int i = 1; i < anISOcurvesArray.size(); i++)
	{
		const auto& curve = anISOcurvesArray[i];
		// 如果等参线之间的最小距离大于 0，那么代表为同向等参线，在这里为 u 向
		if (ComputeCurveCurveDistance(curve, firstCurve) > Precision::Confusion())
			uISOcurvesArray.push_back(curve); // 添加到U向排列等参线数组
		else
			vISOcurvesArray.push_back(curve); // 添加到V向排列等参线数组
	}
	// 根据等参线距离采样中点的距离进行排序
	// 首先找到边界，然后再根据每一条线到边界的距离来排序
	double maxDist = RealFirst();
	Handle(Geom_BSplineCurve) uBoundary; // u 方向边界
	for (const auto& uCurve : uISOcurvesArray)
	{
		// 创建投影对象
		GeomAPI_ProjectPointOnCurve projector(middlePoint, uCurve);
		// 检查是否有投影结果
		if (projector.NbPoints() > 0)
		{
			// 获取距离最短的投影点
			double distance = projector.LowerDistance();
			if (distance > maxDist)
			{
				maxDist = distance;
				uBoundary = uCurve;
			}
		}
	}
	maxDist = RealFirst();
	Handle(Geom_BSplineCurve) vBoundary;
	for (const auto& vCurve : vISOcurvesArray)
	{
		// 创建投影对象
		GeomAPI_ProjectPointOnCurve projector(middlePoint, vCurve);
		// 检查是否有投影结果
		if (projector.NbPoints() > 0)
		{
			// 获取距离最短的投影点
			double distance = projector.LowerDistance();
			if (distance > maxDist)
			{
				maxDist = distance;
				vBoundary = vCurve;
			}
		}
	}
	// 对U向和V向的等参线分别进行排序
	std::sort(uISOcurvesArray.begin(), uISOcurvesArray.end(),
		[&](const Handle(Geom_BSplineCurve)& c1, const Handle(Geom_BSplineCurve)&
			c2) {
				return ComputeCurveCurveDistance(c1, uBoundary) <
					ComputeCurveCurveDistance(c2, uBoundary);
		});
	// 对V向的等参线根据距离边界的距离进行排序
	std::sort(vISOcurvesArray.begin(), vISOcurvesArray.end(),
		[&](const Handle(Geom_BSplineCurve)& c1, const Handle(Geom_BSplineCurve)&
			c2) {
				return ComputeCurveCurveDistance(c1, vBoundary) <
					ComputeCurveCurveDistance(c2, vBoundary);
		});
}

double CompareCurve(const std::vector<gp_Pnt>& points, std::vector<double>& params, Handle(Geom_BSplineCurve)& BsplineCurve
	, std::vector<double>& DisCovParams, bool isSingle, double Toler)
{
	if (points.size() != params.size())
	{
		std::cout << "SIZE NOT EQUAL :IN CXFUNC!" << std::endl;
		return 0;
	}
	double maxError1 = 0;
	double maxParam = 0;
	bool NeedIter = false;
	for (int i = 0; i < params.size(); i++) {
		gp_Pnt pnt = BsplineCurve->Value(params[i]);
		Standard_Real x, y, z;
		pnt.Coord(x, y, z);
		double disTemp = pnt.Distance(points[i]);
		if (disTemp > maxError1)
		{
			maxError1 = disTemp;
			maxParam = params[i];
		}
		if (!isSingle && disTemp > Toler)
		{
			NeedIter = true;
			DisCovParams.push_back(params[i]);
		}
	}
	if (isSingle && maxError1 > Toler)
	{
		DisCovParams.push_back(maxParam);
	}
	return maxError1;
}

int FindSpan(int n, int p, double u, const std::vector<double>& Knots)
{
	if (u == Knots[n + 1])
		return n;
	int low, hign;
	low = p;
	hign = n + 1;
	int mid = (low + hign) / 2;
	while (u < Knots[mid] || u >= Knots[mid + 1])
	{
		if (u < Knots[mid])
		{
			hign = mid;
		}
		else
		{
			low = mid;
		}
		mid = (low + hign) / 2;
	}
	return mid;
}

std::vector<double> KnotGernerationByMergeKnots(const std::vector<double>& KnotsA, const std::vector<double>& KnotsB)
{
	std::vector<double> C;
	std::vector<double> TempB;
	for (size_t i = 0; i < KnotsB.size(); i++)
	{
		if (isEqual(KnotsB[i], 1))
		{
			break;
		}
		if (!isEqual(KnotsB[i], 0))
		{
			TempB.push_back(KnotsB[i]);
		}
	}
	int i = 0, j = 0;
	while (i < KnotsA.size() && j < TempB.size()) {
		if (islessequal(KnotsA[i], TempB[j])) {
			C.push_back(KnotsA[i]);
			i++;
		}
		else {
			C.push_back(TempB[j]);
			j++;
		}
	}

	// 添加剩余元素
	while (i < KnotsA.size()) {
		C.push_back(KnotsA[i]);
		i++;
	}
	while (j < TempB.size()) {
		C.push_back(TempB[j]);
		j++;
	}

	return C;
}

//Uniform Curve Knot to [0,1]
void UniformCurve(Handle(Geom_BSplineCurve)& curve)
{
	TColStd_Array1OfReal curveKnots(1, curve->NbKnots());
	curve->Knots(curveKnots);
	if (!(curveKnots(curveKnots.Lower()) == 0 && curveKnots(curveKnots.Upper()) == 1))
	{
		BSplCLib::Reparametrize(0, 1, curveKnots);
		curve->SetKnots(curveKnots);
	}
}

//To compute the value of a b-spline basic function value 
double OneBasicFun(double u, int i, int p, std::vector<double>& Knots)
{
	double Nip, uleft, uright, saved, temp;
	int m = Knots.size() - 1;
	std::vector<double>N(p + 1);
	if ((i == 0 && isEqual(u, Knots[0])) || (i == m - p - 1 && isEqual(u, Knots[m])))
	{
		return 1.0;
	}

	if (isLessThan(u, Knots[i]) || isGreaterThanOrEqual(u, Knots[i + p + 1]))
	{
		return 0.0;
	}
	for (size_t j = 0; j <= p; j++)
	{
		if (isGreaterThanOrEqual(u, Knots[i + j]) && isLessThan(u, Knots[i + j + 1]))
		{
			N[j] = 1.0;
		}
		else
		{
			N[j] = 0.0;
		}
	}
	for (size_t k = 1; k <= p; k++)
	{
		if (N[0] == 0.0)
		{
			saved = 0.0;
		}
		else
		{
			saved = ((u - Knots[i]) * N[0]) / (Knots[i + k] - Knots[i]);
		}
		for (size_t j = 0; j < p - k + 1; j++)
		{
			uleft = Knots[i + j + 1];
			uright = Knots[i + j + k + 1];
			if (N[j + 1] == 0.0)
			{
				N[j] = saved;
				saved = 0.0;
			}
			else
			{
				temp = N[j + 1] / (uright - uleft);
				N[j] = saved + (uright - u) * temp;
				saved = (u - uleft) * temp;
			}
		}
	}
	Nip = N[0];
	return Nip;
}

//To compute the Res Point Value
gp_Vec CalResPnt(int k, const std::vector<gp_Pnt>& dataPoints, std::vector<double>& parameters, Standard_Integer p,
	std::vector<double>& Knots, int CtrlPntNum) {
	Standard_Real aCoeff1 = OneBasicFun(parameters[k], 0, p, Knots);
	Standard_Real aCoeff2 = OneBasicFun(parameters[k], CtrlPntNum, p, Knots);
	gp_Vec vecTemp0(dataPoints[0].Coord());
	gp_Vec vecTempm(dataPoints[dataPoints.size() - 1].Coord());
	gp_Vec vecTempk(dataPoints[k].Coord());
	gp_Vec vectemp = vecTempk - aCoeff1 * vecTemp0 - aCoeff2 * vecTempm;
	return vectemp;
}

//To compute the Chord length parameterization
void ComputeChordLength(const std::vector<gp_Pnt>& points, std::vector<double>& parameters)
{
	parameters.push_back(0.0);  // Start parameter

	double totalLength = 0.0;
	std::vector<double> segmentLengths;

	// Calculate distances between consecutive points
	for (size_t i = 1; i < points.size(); ++i) {
		double dist = points[i - 1].Distance(points[i]);
		segmentLengths.push_back(dist);
		totalLength += dist;
	}

	// Calculate parameter for each point based on the cumulative length
	double cumulativeLength = 0.0;
	for (size_t i = 0; i < segmentLengths.size(); ++i) {
		cumulativeLength += segmentLengths[i];
		parameters.push_back(cumulativeLength / totalLength);  // Normalize by total length
	}
}

//To trans Sequence to Knots
void sequenceToKnots(const std::vector<double>& sequence, std::vector<double>& knots, std::vector<int>& multiplicities)
{
	if (sequence.empty()) return;

	std::map<double, int> knotMap;

	// 使用map来统计每个节点的重复次数
	for (double value : sequence) 
	{
		bool found = false;
		for (auto& knot : knotMap)
		{
			if (isEqual(value, knot.first, 1e-6))
			{
				knot.second++;
				found = true;
				break;
			}
		}
		if (!found) {
			knotMap[value] = 1;
		}
	}

	// 将map的内容转移到knots和multiplicities向量
	for (const auto& knot : knotMap) 
	{
		knots.push_back(knot.first);
		multiplicities.push_back(knot.second);
	}
}

//To approximate a Bspline curve by a set of points
//in this case, the knot vector(the number of contral point), the degree is known before approximate.
//Assume there are m data points.We need to use n( CtrlPntNum)+1 to approximate the m data points .
//CtrlPntNum n
Handle(Geom_BSplineCurve) ApproximateC(const std::vector<gp_Pnt>& Pnts, std::vector<double>& PntParams, TColStd_Array1OfReal& Knots, TColStd_Array1OfInteger& Mutis, std::vector<double>& FKnots,
	int degree)
{
	int n = FKnots.size() - degree - 2;
	int m = Pnts.size() - 1;
	//1.Chord Parameterized
	if (PntParams.size() == 0)
	{
		ComputeChordLength(Pnts, PntParams);
	}
	//(N^T*N)P=R

	//2.Construct matrix N
	Eigen::MatrixXd matN(m - 1, n - 1);
	for (size_t i = 0; i < m - 1; i++)
	{
		for (size_t j = 0; j < n - 1; j++)
		{
			double value = OneBasicFun(PntParams[i + 1], j + 1, degree, FKnots);
			matN(i, j) = value;
		}
	}
	Eigen::MatrixXd matTransposeN = matN.transpose();
	Eigen::MatrixXd NTN = matTransposeN * matN;

	//3.Construct matrix R
	//3.1 Compute Ri(2-(m-1))
	Eigen::VectorXd VRx(m - 1);
	Eigen::VectorXd VRy(m - 1);
	Eigen::VectorXd VRz(m - 1);
	for (size_t i = 1; i <= m - 1; i++)
	{
		gp_Vec VecTemp = CalResPnt(i, Pnts, PntParams, degree, FKnots, n);
		double x, y, z;
		VecTemp.Coord(x, y, z);
		VRx(i - 1) = x;
		VRy(i - 1) = y;
		VRz(i - 1) = z;
	}
	//3.2 Compute the component of R
	Eigen::VectorXd Rx = matTransposeN * VRx;
	Eigen::VectorXd Ry = matTransposeN * VRy;
	Eigen::VectorXd Rz = matTransposeN * VRz;

	//4.solve NtN P = R
	Eigen::VectorXd Sx = NTN.lu().solve(Rx);
	Eigen::VectorXd Sy = NTN.lu().solve(Ry);
	Eigen::VectorXd Sz = NTN.lu().solve(Rz);


	//5.construct bspline curve
	TColgp_Array1OfPnt ctrlPnts(1, n + 1);
	ctrlPnts[1] = Pnts[0];
	ctrlPnts[n + 1] = Pnts[m];
	for (size_t i = 2; i <= n; i++)
	{
		gp_Pnt pntTemp(Sx(i - 2), Sy(i - 2), Sz(i - 2));
		ctrlPnts[i] = pntTemp;
	}
	Handle(Geom_BSplineCurve) bspline = new Geom_BSplineCurve(ctrlPnts, Knots, Mutis, degree);
	return bspline;
}

//To approximate a Bspline curve by a set of points
	//in this case, the knot vector(the number of contral point), the degree is known before approximate.
	//Assume there are m data points.We need to use n( CtrlPntNum)+1 to approximate the m data points .
	//CtrlPntNum n
Handle(Geom_BSplineCurve) ApproximateC(const std::vector<gp_Pnt>& Pnts, std::vector<double>& params, std::vector<double>& FKnots, int degree)
{
	std::vector<double> Knots;
	std::vector<int> Mutis;
	sequenceToKnots(FKnots, Knots, Mutis);
	TColStd_Array1OfReal Knots_OCC(1, Knots.size());
	TColStd_Array1OfInteger Mutis_OCC(1, Mutis.size());
	for (size_t i = 0; i < Knots.size(); i++)
	{
		Knots_OCC[i + 1] = Knots[i];
		Mutis_OCC[i + 1] = Mutis[i];
	}
	return ApproximateC(Pnts, params, Knots_OCC, Mutis_OCC, FKnots, degree);
}

void SurfaceModelingTool::CreateLoftingSurface(const std::vector<Handle(Geom_BSplineCurve)>& curvesArray, const std::vector<gp_Vec>& normals, std::vector<TopoDS_Shape>& loftingSurfaces)
{
	double offsetDistance = 400;
	for (int i = 0; i < curvesArray.size(); i++)
	{
		TColgp_Array1OfPnt aPnts1(1, curvesArray[i]->NbPoles());
		TColgp_Array1OfPnt aPnts2(1, curvesArray[i]->NbPoles());

		for (int j = 0; j < curvesArray[i]->NbPoles(); j++)
		{
			aPnts1.SetValue(j + 1, curvesArray[i]->Pole(j + 1).Translated(normals[i] * offsetDistance));
			aPnts2.SetValue(j + 1, curvesArray[i]->Pole(j + 1).Translated(-normals[i] * offsetDistance));
		}

		// 创建 B 样条曲线对象
		Handle(Geom_BSplineCurve) aBSplineCurve1 = new Geom_BSplineCurve(
			aPnts1,
			curvesArray[i]->Knots(),
			curvesArray[i]->Multiplicities(),
			curvesArray[i]->Degree()
		);
		Handle(Geom_BSplineCurve) aBSplineCurve2 = new Geom_BSplineCurve(
			aPnts2,
			curvesArray[i]->Knots(),
			curvesArray[i]->Multiplicities(),
			curvesArray[i]->Degree()
		);

		BRepBuilderAPI_MakeWire aWireMaker0, aWireMaker1, aWireMaker2;
		BRepBuilderAPI_MakeVertex aStartVertex0(curvesArray[i]->Pole(1));
		BRepBuilderAPI_MakeVertex aEndVertex0(curvesArray[i]->Pole(curvesArray[i]->NbPoles()));
		TopoDS_Edge aEdge0 = BRepBuilderAPI_MakeEdge(curvesArray[i], aStartVertex0, aEndVertex0);
		aWireMaker0.Add(aEdge0);

		BRepBuilderAPI_MakeVertex aStartVertex1(aBSplineCurve1->Pole(1));
		BRepBuilderAPI_MakeVertex aEndVertex1(aBSplineCurve1->Pole(aBSplineCurve1->NbPoles()));
		TopoDS_Edge aEdge1 = BRepBuilderAPI_MakeEdge(aBSplineCurve1, aStartVertex1, aEndVertex1);
		aWireMaker1.Add(aEdge1);

		BRepBuilderAPI_MakeVertex aStartVertex2(aBSplineCurve2->Pole(1));
		BRepBuilderAPI_MakeVertex aEndVertex2(aBSplineCurve2->Pole(aBSplineCurve2->NbPoles()));
		TopoDS_Edge aEdge2 = BRepBuilderAPI_MakeEdge(aBSplineCurve2, aStartVertex2, aEndVertex2);
		aWireMaker2.Add(aEdge2);

		BRepOffsetAPI_ThruSections aLoftSurface(Standard_True, Standard_False);
		aLoftSurface.AddWire(aWireMaker1.Wire());
		aLoftSurface.AddWire(aWireMaker0.Wire());
		aLoftSurface.AddWire(aWireMaker2.Wire());

		aLoftSurface.Build();
		if (!aLoftSurface.IsDone())
		{
			// 输出异常
		}
		else
		{
			loftingSurfaces.emplace_back(aLoftSurface.Shape());
		}
	}
}

Handle(Geom_BSplineCurve) IterateApproximate(std::vector<double>& InsertKnots, const std::vector<gp_Pnt>& Pnts, std::vector<double>& PntsParams, std::vector<double>& InitKnots, int degree, int MaxIterNum, double toler)
{
	int itNum = 1;
	double currentMaxError = 100;
	Handle(Geom_BSplineCurve) IterBspineCurve;
	std::vector<double> CurrentKnots = InitKnots;
	std::cout << std::endl;
	std::cout << "----------------Strat Iteration----------------" << std::endl;
	std::cout << "----------------Iteration Infor----------------" << std::endl;
	std::cout << "Max Iteration Num = " << MaxIterNum << " " << std::endl;
	std::cout << "Iteration degree = " << degree << " " << std::endl;
	std::cout << "Iteration toler = " << toler << " " << std::endl;
	std::cout << "----------------Strat----------------" << std::endl;
	while (currentMaxError > toler && itNum <= MaxIterNum)
	{
		auto start = std::chrono::high_resolution_clock::now();

		IterBspineCurve = ApproximateC(Pnts, PntsParams, CurrentKnots, degree);

		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> duration = end - start;

		KnotUpdate knotUpdate(IterBspineCurve, CurrentKnots, Pnts, PntsParams);
		auto newKnot = knotUpdate.SelfSingleUpdate(PARAM_BASED_BY_INTERVAL_ERROR);
		currentMaxError = knotUpdate.getMaxError();
		std::cout << "Iteration Num = " << itNum << " The Current Error = " << currentMaxError << " The Target Error = " << toler << " The Time = " << duration.count() << " ms " << std::endl;
		if (currentMaxError > toler)
		{
			//update knot vector
			InsertKnots.push_back(newKnot);
			CurrentKnots = knotUpdate.getSequences();
		}
		else
		{
			std::cout << "----------------End----------------" << std::endl;
			std::cout << "Find Curve,iteration ending--------------the current error is " << currentMaxError << std::endl;
			std::cout << "----------------End Iteration----------------" << std::endl << std::endl;
			return IterBspineCurve;
		}
		itNum++;
	}
	//set check params
	std::cout << "----------------End----------------" << std::endl;
	std::cout << "Arrive max iteration number, iteration ending--------------the current error is " << currentMaxError << std::endl;
	std::cout << "----------------End Iteration----------------" << std::endl << std::endl;

	//return bspline;
	return IterBspineCurve;
}
double CalPointsChordLen(const std::vector<gp_Pnt>& points)
{
	if (points.size() < 2) {
		return 0.0;
	}
	double totalLength = 0.0;
	for (size_t i = 1; i < points.size(); ++i) {
		totalLength += points[i - 1].Distance(points[i]);
	}
	return totalLength;
}
gp_Vec CalTangent(const std::vector<gp_Pnt>& points, double TanMagnitude)
{
	if (points.size() != 3) {
		throw std::invalid_argument("输入点的数量必须为3个。");
	}

	gp_Pnt P0 = points[0];
	gp_Pnt P1 = points[1];
	gp_Pnt P2 = points[2];

	// B1 = 2 * P1 - 0.5 * P0 - 0.5 * P2
	gp_Vec vec_P0_P1(P0, P1); // P1 - P0
	gp_Vec vec_P2_P1(P2, P1); // P1 - P2

	gp_Pnt B1(
		2.0 * P1.X() - 0.5 * P0.X() - 0.5 * P2.X(),
		2.0 * P1.Y() - 0.5 * P0.Y() - 0.5 * P2.Y(),
		2.0 * P1.Z() - 0.5 * P0.Z() - 0.5 * P2.Z()
	);

	// 计算切向量 B'(0) = 2 * (B1 - B0)
	gp_Vec tangentVec(P0, B1); // B1 - B0
	tangentVec.Multiply(2.0);

	double magnitude = tangentVec.Magnitude();
	if (magnitude == 0.0) {
		throw std::runtime_error("切向量的长度为零，无法缩放。");
	}

	tangentVec.Normalize();
	tangentVec.Multiply(TanMagnitude);

	return tangentVec;
}
void SurfaceModelingTool::LoftSurfaceIntersectWithCurve(const std::vector<TopoDS_Shape>& LoftingSur, 
	const std::vector<Handle(Geom_BSplineCurve)>& ISOcurvesArray_Initial, 
	const std::vector<Handle(Geom_BSplineCurve)>& anInternalBSplineCurves, 
	std::vector<Handle(Geom_BSplineCurve)>& ISOcurvesArray_New, 
	Standard_Integer isoCount,
	std::vector<std::vector<gp_Pnt>>& InterpolatePoints,
	std::vector<TopoDS_Edge>& TangentArray1,
	std::vector<TopoDS_Edge>& TangentArray2,
	Handle(Geom_BSplineSurface) CoonsSurface)
{
	std::vector<std::vector<gp_Pnt>> debugPoints;
	for (int i = 0; i < LoftingSur.size(); i++)
	{
		std::vector<gp_Pnt> aPntsVector; // 动态存储交点和端点
		std::vector<gp_Pnt> aInterPnts;  // 每个LoftingSur与内部线的交点

		// 遍历Lofting曲面
		TopExp_Explorer explorer2(LoftingSur[i], TopAbs_FACE);
		for (; explorer2.More(); explorer2.Next())
		{
			const TopoDS_Face& aFace = TopoDS::Face(explorer2.Current());
			Handle(Geom_Surface) aSur = BRep_Tool::Surface(aFace);
			Handle(Geom_BSplineSurface) aLoftingSur = Handle(Geom_BSplineSurface)::DownCast(aSur);

			if (!aLoftingSur.IsNull())
			{
				// 遍历内部的B样条曲线，计算交点
				for (int j = 0; j < anInternalBSplineCurves.size(); j++)
				{
					GeomAPI_IntCS anInterCS(anInternalBSplineCurves[j], aLoftingSur);
					if (anInterCS.IsDone())
					{
						if (anInterCS.NbPoints())
						{
							for (int k = 1; k <= anInterCS.NbPoints(); k++)
							{
								aInterPnts.emplace_back(anInterCS.Point(k));
							}
						}
					}
				}
			}
		}

		// 插入起点、交点和终点
		if (aInterPnts.size() > 0)
		{
			gp_Pnt startPoint = ISOcurvesArray_Initial[i]->StartPoint();
			gp_Pnt endPoint = ISOcurvesArray_Initial[i]->EndPoint();
			aPntsVector.insert(aPntsVector.end(), aInterPnts.begin(), aInterPnts.end()); // 交点

			// 按照与startPoint的距离从小到大排序，主要事保证方向
			std::sort(aPntsVector.begin(), aPntsVector.end(), [&startPoint](const gp_Pnt& p1, const gp_Pnt& p2)
			{
					return p1.Distance(startPoint) < p2.Distance(startPoint);
			});

			aPntsVector.insert(aPntsVector.begin(), startPoint);  // 起点
			aPntsVector.push_back(endPoint);    // 终点

			for (int j = 1; j < aPntsVector.size() - 1; j++)
			{
				gp_Pnt lastPnt = aPntsVector[j - 1];
				gp_Pnt Pnt = aPntsVector[j];
				gp_Pnt nextPnt = aPntsVector[j + 1];
				if (!(Pnt.Distance(lastPnt) > startPoint.Distance(endPoint) / (isoCount * 2) &&
					Pnt.Distance(nextPnt) > startPoint.Distance(endPoint) / (isoCount * 2)))
				{
					aPntsVector.erase(aPntsVector.begin() + j);
					j--;
				}
			}
			InterpolatePoints.push_back(aPntsVector);

			// startPoint切线和endPoint的切线
			gp_Vec FirstD1, LastD1;
			gp_Vec startDirection = aPntsVector[1].XYZ() - aPntsVector[0].XYZ();
			startDirection.Normalize();
			gp_Vec endDirection = aPntsVector[aPntsVector.size() - 1].XYZ() - aPntsVector[aPntsVector.size() - 2].XYZ();
			endDirection.Normalize();

			if (!CoonsSurface.IsNull())
			{
				gp_Vec U_StartTangent, V_StartTangent;
				GeomAPI_ProjectPointOnSurf projector(aPntsVector.front(), CoonsSurface);

				if (projector.NbPoints() > 0)
				{
					gp_Pnt closestPoint = projector.NearestPoint();
					double uParam, vParam;
					projector.LowerDistanceParameters(uParam, vParam);

					// 获取U和V方向的偏导数（切向量）
					CoonsSurface->D1(uParam, vParam, closestPoint, U_StartTangent, V_StartTangent);

					// 计算法向量（U和V偏导数的叉积）
					gp_Vec normalVector = U_StartTangent.Crossed(V_StartTangent);
					normalVector = normalVector.Normalized();
					if (startDirection.Dot(normalVector) < 0)
					{
						normalVector.Reverse(); // 反向法向量
					}

					// 计算startDirection在法向量上的投影分量
					double projectionMagnitude = startDirection.Dot(normalVector) / normalVector.Magnitude();
					gp_Vec projection = normalVector * projectionMagnitude; // 计算投影向量

					// 计算startDirection在平面上的投影（去掉法向量的分量）
					gp_Vec projectedDirection = startDirection - projection;
					projectedDirection.Normalize();


					// 计算投影后的切线
					FirstD1 = projectedDirection;
					FirstD1.Multiply(CalPointsChordLen(aPntsVector) / FirstD1.Magnitude());

					// 生成切线边界
					TangentArray1.push_back(BRepBuilderAPI_MakeEdge(closestPoint, closestPoint.Translated(FirstD1 * 0.1)).Edge());
				}
			}

			if (!CoonsSurface.IsNull())
			{
				gp_Vec U_EndTangent, V_EndTangent;
				GeomAPI_ProjectPointOnSurf projector(aPntsVector.back(), CoonsSurface);

				if (projector.NbPoints() > 0)
				{
					gp_Pnt closestPoint = projector.NearestPoint();
					double uParam, vParam;
					projector.LowerDistanceParameters(uParam, vParam);

					// 获取U和V方向的偏导数（切向量）
					CoonsSurface->D1(uParam, vParam, closestPoint, U_EndTangent, V_EndTangent);

					// 计算法向量（U和V偏导数的叉积）
					gp_Vec normalVector = U_EndTangent.Crossed(V_EndTangent);
					normalVector = normalVector.Normalized();
					if (normalVector.Dot(endDirection) < 0)
					{
						normalVector.Reverse();
					}
					// 计算endDirection在法向量上的投影分量
					double projectionMagnitude = endDirection.Dot(normalVector) / normalVector.Magnitude();
					gp_Vec projection = normalVector * projectionMagnitude; // 计算投影向量

					// 计算endDirection在平面上的投影（去掉法向量的分量）
					gp_Vec projectedDirection = endDirection - projection;
					projectedDirection.Normalize();
					// 计算投影后的切线
					LastD1 = projectedDirection;
					LastD1.Multiply(CalPointsChordLen(aPntsVector) / LastD1.Magnitude());

					// 生成切线边界
					TangentArray1.push_back(BRepBuilderAPI_MakeEdge(closestPoint, closestPoint.Translated(LastD1 * 0.1)).Edge());
				}
			}

			// 调整startDirection和endDirection的长度
			startDirection.Multiply(CalPointsChordLen(aPntsVector) / startDirection.Magnitude());
			endDirection.Multiply(CalPointsChordLen(aPntsVector) / endDirection.Magnitude());

			Handle(TColgp_HArray1OfPnt) points = new TColgp_HArray1OfPnt(1, aPntsVector.size());
			for (int j = 0; j < aPntsVector.size(); j++)
			{
				points->SetValue(j + 1, aPntsVector[j]);
			}
			debugPoints.push_back(aPntsVector);
			GeomAPI_Interpolate interpolate(points, Standard_False, 1.0e-3);
			interpolate.Load(FirstD1, LastD1, Standard_True);
			// 执行插值计算
			interpolate.Perform();
			// 检查是否成功完成插值
			if (interpolate.IsDone())
			{
				// 获取插值后的曲线对象
				Handle(Geom_BSplineCurve) aBSplineCurve = interpolate.Curve();
				ISOcurvesArray_New.emplace_back(aBSplineCurve);
			}
		}
	}
}

std::vector<double> GetKnotsSequence(Handle(Geom_BSplineCurve) curve)
{
	std::vector<double> debugKnots;
	TColStd_Array1OfReal KnotsSequence = curve->KnotSequence();
	for (int i = KnotsSequence.Lower(); i <= KnotsSequence.Upper(); i++)
	{
		debugKnots.push_back(KnotsSequence.Value(i));
	}
	return debugKnots;
}

std::vector<double> ComputeUniformParam(int numSamples, double left, double right) {
	std::vector<double> parameters;
	if (numSamples == 0) {
		return parameters;
	}
	for (int i = 1; i <= numSamples; i++) {
		Standard_Real param = left + (right - left) * (i - 1) / (numSamples - 1);
		parameters.push_back(param);
	}
	return parameters;
}

std::vector<double> KnotGernerationByParams(const std::vector<double>& params, int n, int p)
{
	int m = params.size() - 1;
	double d = (m + 1) / (n - p + 1);
	std::vector<double> Knots(n + p + 2);
	int temp;
	double alpha;
	for (size_t i = 0; i <= p; i++)
	{
		Knots[i] = 0.0;
	}
	for (size_t j = 1; j <= n - p; j++)
	{
		temp = int(j * d);
		alpha = j * d - temp;
		Knots[p + j] = (1 - alpha) * params[temp - 1] + alpha * params[temp];
	}
	for (size_t i = n + 1; i <= n + p + 1; i++)
	{
		Knots[i] = 1;
	}
	return Knots;
}
//To compute the Res Point Value - first d1 case
gp_Vec CalResPnt(int k, const std::vector<gp_Pnt>& dataPoints, const gp_Pnt& SecondPoint, const gp_Pnt& LastSecondPoint, const std::vector<double>& parameters, Standard_Integer p,
	std::vector<double>& Knots, int CtrlPntNum) {
	Standard_Real aCoeff0 = OneBasicFun(parameters[k], 0, p, Knots);
	Standard_Real aCoeff1 = OneBasicFun(parameters[k], 1, p, Knots);
	Standard_Real aCoeffms1 = OneBasicFun(parameters[k], CtrlPntNum - 1, p, Knots);
	Standard_Real aCoeffm = OneBasicFun(parameters[k], CtrlPntNum, p, Knots);
	gp_Vec vecTemp0(dataPoints[0].Coord());
	gp_Vec vecTemp1(SecondPoint.Coord());
	gp_Vec vecTempms1(LastSecondPoint.Coord());
	gp_Vec vecTempm(dataPoints.back().Coord());
	gp_Vec vecTempk(dataPoints[k].Coord());
	gp_Vec vectemp = vecTempk - aCoeff0 * vecTemp0 - aCoeff1 * vecTemp1 - aCoeffms1 * vecTempms1 - aCoeffm * vecTempm;
	return vectemp;
}

Handle(Geom_BSplineCurve) EndDerivaConstraintBsplineCurveAppro(const std::vector<gp_Pnt>& Pnts, const gp_Vec& FirstD1, const gp_Vec& LastD1,
	std::vector<double>& Params, std::vector<double>& KnotSequences, int degree) {
	//check the number must large than 3
	if (Pnts.size() < 3) {
		std::cerr << "the number of data points must great than 3!" << std::endl;
		return nullptr;
	}
	//compute the second point
	gp_Vec first_point_vectype = gp_Vec(Pnts[0].XYZ());
	gp_Vec second_point_vectype = first_point_vectype + (KnotSequences[degree + 1] / (double)degree) * FirstD1;
	gp_Pnt second_point = gp_Pnt(second_point_vectype.XYZ());

	//compute the second point
	int index = KnotSequences.size() - degree - 2;
	gp_Vec last_point_vectype = gp_Vec(Pnts.back().XYZ());
	gp_Vec last_second_point_vectype = last_point_vectype - ((1 - KnotSequences[index]) / (double)degree) * LastD1;
	gp_Pnt last_second_point = gp_Pnt(last_second_point_vectype.XYZ());

	int n = KnotSequences.size() - degree - 2;
	int m = Pnts.size() - 1;

	// Construct matrix N
	Eigen::MatrixXd matN = Eigen::MatrixXd::Zero(m - 1, n - 3);
	for (int i = 0; i < m - 1; ++i) {
		for (int j = 0; j < n - 3; ++j) {
			matN(i, j) = OneBasicFun(Params[i + 1], j + 2, degree, KnotSequences);
		}
	}

	// Construct matrix R for x, y, z components
	Eigen::MatrixXd VR(3, m - 1);
	for (int i = 1; i <= m - 1; ++i) {
		gp_Vec VecTemp = CalResPnt(i, Pnts, second_point, last_second_point, Params, degree, KnotSequences, n);
		double x, y, z;
		VecTemp.Coord(x, y, z);
		VR(0, i - 1) = x;
		VR(1, i - 1) = y;
		VR(2, i - 1) = z;
	}

	// Solve NtN * P = R for x, y, z components
	Eigen::MatrixXd S = matN.householderQr().solve(VR.transpose()).transpose();

	// Construct BSpline control points
	TColgp_Array1OfPnt ctrlPnts(1, n + 1);
	ctrlPnts.SetValue(1, Pnts[0]);
	ctrlPnts.SetValue(2, second_point);
	ctrlPnts.SetValue(n, last_second_point);
	ctrlPnts.SetValue(n + 1, Pnts[m]);
	for (int i = 3; i <= n - 1; ++i) {
		gp_Pnt pntTemp(S(0, i - 3), S(1, i - 3), S(2, i - 3));
		ctrlPnts.SetValue(i, pntTemp);
	}

	std::vector<double> Knots;
	std::vector<int> Mutis;
	sequenceToKnots(KnotSequences, Knots, Mutis);
	TColStd_Array1OfReal Knots_OCC(1, Knots.size());
	TColStd_Array1OfInteger Mutis_OCC(1, Mutis.size());
	for (size_t i = 0; i < Knots.size(); ++i) {
		Knots_OCC.SetValue(static_cast<Standard_Integer>(i + 1), Knots[i]);
		Mutis_OCC.SetValue(static_cast<Standard_Integer>(i + 1), Mutis[i]);
	}
	Handle(Geom_BSplineCurve) bspline = new Geom_BSplineCurve(ctrlPnts, Knots_OCC, Mutis_OCC, degree);
	return bspline;
}

Handle(Geom_BSplineCurve) IterateApproximate(std::vector<double>& InsertKnots,
	const std::vector<gp_Pnt>& Pnts, 
	const gp_Vec& FirstD1, const gp_Vec& LastD1, 
	std::vector<double>& PntsParams, std::vector<double>& InitKnots, 
	int degree, int MaxIterNum, double toler) {
	int itNum = 1;
	double currentMaxError = 100;
	Handle(Geom_BSplineCurve) IterBspineCurve;
	std::vector<double> CurrentKnots = InitKnots;
	std::cout << std::endl;
	std::cout << "----------------Strat Iteration----------------" << std::endl;
	std::cout << "----------------Iteration Infor----------------" << std::endl;
	std::cout << "Max Iteration Num = " << MaxIterNum << " " << std::endl;
	std::cout << "Iteration degree = " << degree << " " << std::endl;
	std::cout << "Iteration toler = " << toler << " " << std::endl;
	std::cout << "----------------Strat----------------" << std::endl;
	while (currentMaxError > toler && itNum <= MaxIterNum) {
		auto start = std::chrono::high_resolution_clock::now();

		IterBspineCurve = EndDerivaConstraintBsplineCurveAppro(Pnts, FirstD1, LastD1, PntsParams, CurrentKnots, degree);

		auto end = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> duration = end - start;

		KnotUpdate knotUpdate(IterBspineCurve, CurrentKnots, Pnts, PntsParams);
		auto newKnot = knotUpdate.SelfSingleUpdate(PARAM_BASED_BY_INTERVAL_ERROR);
		currentMaxError = knotUpdate.getMaxError();
		std::cout << "Iteration Num = " << itNum << " The Current Error = " << currentMaxError << " The Target Error = " << toler << " The Time = " << duration.count() << " ms " << std::endl;
		if (currentMaxError > toler) {
			//update knot vector
			InsertKnots.push_back(newKnot);
			CurrentKnots = knotUpdate.getSequences();
		}
		else {
			std::cout << "----------------End----------------" << std::endl;
			std::cout << "Find Curve,iteration ending--------------the current error is " << currentMaxError << std::endl;
			std::cout << "----------------End Iteration----------------" << std::endl << std::endl;
			return IterBspineCurve;
		}
		itNum++;
	}
	//set check params
	std::cout << "----------------End----------------" << std::endl;
	std::cout << "Arrive max iteration number, iteration ending--------------the current error is " << currentMaxError << std::endl;
	std::cout << "----------------End Iteration----------------" << std::endl << std::endl;

	//return bspline;
	return IterBspineCurve;
}

//To compute the Res Point Value - first d1 case
gp_Vec CalResPnt(int k, const std::vector<gp_Pnt>& dataPoints, const gp_Pnt& SecondPoint, const std::vector<double>& parameters, Standard_Integer p,
	std::vector<double>& Knots, int CtrlPntNum) {
	Standard_Real aCoeff0 = OneBasicFun(parameters[k], 0, p, Knots);
	Standard_Real aCoeff1 = OneBasicFun(parameters[k], 1, p, Knots);
	Standard_Real aCoeffm = OneBasicFun(parameters[k], CtrlPntNum, p, Knots);
	gp_Vec vecTemp0(dataPoints[0].Coord());
	gp_Vec vecTemp1(SecondPoint.Coord());
	gp_Vec vecTempm(dataPoints.back().Coord());
	gp_Vec vecTempk(dataPoints[k].Coord());
	gp_Vec vectemp = vecTempk - aCoeff0 * vecTemp0 - aCoeff1 * vecTemp1 - aCoeffm * vecTempm;
	return vectemp;
}

bool CompareDistance(std::pair<double, gp_Pnt> p1, std::pair<double, gp_Pnt> p2)
{
	return p1.first < p2.first;
}

//std::pair<double, double> processPoints(const gp_Pnt& P1, const gp_Pnt& P2, const std::vector<gp_Pnt>& isoInterpolatePoints, const std::vector<gp_Pnt>& oppsiteInterpolatePoints)
//{
//	std::vector<std::pair<double, gp_Pnt>> distancesP1;
//	for (const auto& point : isoInterpolatePoints)
//	{
//		distancesP1.push_back({ P1.Distance(point), point });
//	}
//
//	std::sort(distancesP1.begin(), distancesP1.end(), CompareDistance);
//
//	gp_Pnt nearestPoint1_P1 = distancesP1[0].second;
//	gp_Pnt nearestPoint2_P1 = distancesP1[1].second;
//
//	// 计算夹角，确保两个点分布在当前点两侧
//	gp_Vec v1_P1(nearestPoint1_P1, P1);
//	gp_Vec v2_P1(nearestPoint2_P1, P1);
//	double cosAngle_P1 = v1_P1.Dot(v2_P1) / (v1_P1.Magnitude() * v2_P1.Magnitude());
//
//	int next = 2;
//	while (cosAngle_P1 >= 0)
//	{
//		nearestPoint2_P1 = distancesP1[next++].second;  // 找到下一个最近的点
//		v1_P1 = gp_Vec(nearestPoint1_P1, P1);
//		v2_P1 = gp_Vec(nearestPoint2_P1, P1);
//		cosAngle_P1 = v1_P1.Dot(v2_P1) / (v1_P1.Magnitude() * v2_P1.Magnitude());
//	}
//
//	std::vector<std::pair<double, gp_Pnt>> distancesP2;
//	for (const auto& point : oppsiteInterpolatePoints)
//	{
//		distancesP2.push_back({ P2.Distance(point), point });
//	}
//
//	std::sort(distancesP2.begin(), distancesP2.end(), CompareDistance);
//
//	gp_Pnt nearestPoint1_P2 = distancesP2[0].second;
//	gp_Pnt nearestPoint2_P2 = distancesP2[1].second;
//
//	// 计算夹角，确保两个点分布在当前点两侧
//	gp_Vec v1_P2(nearestPoint1_P2, P2);
//	gp_Vec v2_P2(nearestPoint2_P2, P2);
//	double cosAngle_P2 = v1_P2.Dot(v2_P2) / (v1_P2.Magnitude() * v2_P2.Magnitude());
//
//	next = 2;
//	while (cosAngle_P2 >= 0)
//	{
//		nearestPoint2_P2 = distancesP2[next++].second;  // 找到下一个最近的点
//		v1_P2 = gp_Vec(nearestPoint1_P2, P2);
//		v2_P2 = gp_Vec(nearestPoint2_P2, P2);
//		cosAngle_P2 = v1_P2.Dot(v2_P2) / (v1_P2.Magnitude() * v2_P2.Magnitude());
//	}
//
//	// 计算半径
//	double L1 = P1.Distance(nearestPoint1_P1) + P1.Distance(nearestPoint2_P1);
//	double L2 = P2.Distance(nearestPoint1_P2) + P2.Distance(nearestPoint2_P2);
//	double radius = (L1 + L2) / 2;
//	int M = 0;
//	double w1, w2;
//	if (L1 > L2)
//	{
//		for (const auto& point : oppsiteInterpolatePoints)
//		{
//			if (P2.Distance(point) <= radius)
//				M++;
//		}
//		if (M >= 3)
//		{
//			w2 = 1;
//		}
//		else
//		{
//			M += 1;
//			w2 = M / (M + 1);
//		}
//		return std::make_pair(1-w2, w2);
//	}
//	else
//	{
//		for (const auto& point : isoInterpolatePoints)
//		{
//			if (P1.Distance(point) <= radius)
//				M++;
//		}
//		if (M >= 3)
//		{
//			w1 = 1;
//		}
//		else
//		{
//			M += 1;
//			w1 = M / (M + 1);
//		}
//		return std::make_pair(w1, 1 - w1);
//	}
//}
std::pair<double, double> processPoints(const gp_Pnt& P1, const gp_Pnt& P2, const std::vector<gp_Pnt>& isoInterpolatePoints, const std::vector<gp_Pnt>& oppsiteInterpolatePoints)
{
	std::vector<std::pair<double, gp_Pnt>> distancesP1;
	for (const auto& point : isoInterpolatePoints)
	{
		distancesP1.push_back({ P1.Distance(point), point });
	}

	std::sort(distancesP1.begin(), distancesP1.end(), CompareDistance);

	gp_Pnt nearestPoint1_P1 = distancesP1[0].second;
	gp_Pnt nearestPoint2_P1 = distancesP1[1].second;

	// 计算连线向量
	gp_Vec lineVec(nearestPoint1_P1, nearestPoint2_P1);
	gp_Vec vecToP1(nearestPoint1_P1, P1);

	// 计算投影
	double projectionLength = vecToP1.Dot(lineVec) / lineVec.Magnitude();

	// 判断投影是否在连线区间内
	bool isOppositeSide = false;
	if (projectionLength >= 0 && projectionLength <= lineVec.Magnitude())
	{
		isOppositeSide = true;  // 如果投影在连线区间内，认为两点在两侧
	}

	// 如果两点在同一侧，继续查找下一个点
	int next = 2;
	while (!isOppositeSide && next < distancesP1.size())  // 检查索引是否越界
	{
		nearestPoint2_P1 = distancesP1[next++].second;
		lineVec = gp_Vec(nearestPoint1_P1, nearestPoint2_P1);
		vecToP1 = gp_Vec(nearestPoint1_P1, P1);
		projectionLength = vecToP1.Dot(lineVec) / lineVec.Magnitude();

		if (projectionLength >= 0 && projectionLength <= lineVec.Magnitude())
		{
			isOppositeSide = true;  // 找到满足条件的点
		}
	}

	// 计算P2的最近点
	std::vector<std::pair<double, gp_Pnt>> distancesP2;
	for (const auto& point : oppsiteInterpolatePoints)
	{
		distancesP2.push_back({ P2.Distance(point), point });
	}

	std::sort(distancesP2.begin(), distancesP2.end(), CompareDistance);

	gp_Pnt nearestPoint1_P2 = distancesP2[0].second;
	gp_Pnt nearestPoint2_P2 = distancesP2[1].second;

	// 计算连线向量
	gp_Vec lineVec_P2(nearestPoint1_P2, nearestPoint2_P2);
	gp_Vec vecToP2(nearestPoint1_P2, P2);

	// 计算投影
	double projectionLength_P2 = vecToP2.Dot(lineVec_P2) / lineVec_P2.Magnitude();  // 归一化投影长度，防止除以线段的长度

	// 判断投影是否在连线区间内（0 <= projectionLength_P2 <= lineVec_P2.Magnitude()）
	bool isOppositeSide_P2 = false;
	if (projectionLength_P2 >= 0 && projectionLength_P2 <= lineVec_P2.Magnitude())
	{
		isOppositeSide_P2 = true;  // 如果投影在连线区间内，认为两点在两侧
	}

	// 如果两点在同一侧，继续查找下一个点
	int nextP2 = 2;
	while (!isOppositeSide_P2 && nextP2 < distancesP2.size())  // 检查索引是否越界
	{
		nearestPoint2_P2 = distancesP2[nextP2++].second;
		lineVec_P2 = gp_Vec(nearestPoint1_P2, nearestPoint2_P2);
		vecToP2 = gp_Vec(nearestPoint1_P2, P2);
		projectionLength_P2 = vecToP2.Dot(lineVec_P2) / lineVec_P2.Magnitude();

		if (projectionLength_P2 >= 0 && projectionLength_P2 <= lineVec_P2.Magnitude())
		{
			isOppositeSide_P2 = true;  // 找到满足条件的点
		}
	}

	// 计算半径
	double L1 = P1.Distance(nearestPoint1_P1) + P1.Distance(nearestPoint2_P1);
	double L2 = P2.Distance(nearestPoint1_P2) + P2.Distance(nearestPoint2_P2);
	double radius = (L1 + L2) / 2;
	int M = 0;
	double w1, w2;

	if (L1 > L2)
	{
		for (const auto& point : oppsiteInterpolatePoints)
		{
			if (P2.Distance(point) <= radius)
				M++;
		}
		if (M >= 3)
		{
			w2 = 1;
		}
		else
		{
			M += 1;
			w2 = M / (M + 1);
		}
		return std::make_pair(1 - w2, w2);
	}
	else
	{
		for (const auto& point : isoInterpolatePoints)
		{
			if (P1.Distance(point) <= radius)
				M++;
		}
		if (M >= 3)
		{
			w1 = 1;
		}
		else
		{
			M += 1;
			w1 = M / (M + 1);
		}
		return std::make_pair(w1, 1 - w1);
	}
}

void ProcessISOCurvesWithTangent(
	const std::vector<Handle(Geom_BSplineCurve)>& isoCurvesArray_New,
	const std::vector<Handle(Geom_BSplineCurve)>& oppsiteISOcurvesArray_New,
	std::vector<std::vector<gp_Pnt>>& isoInterpolatePoints,
	std::vector<std::vector<gp_Pnt>>& oppsiteInterpolatePoints,
	std::vector<Handle(Geom_BSplineCurve)>& isoCurvesArray_Final,
	std::vector<std::vector<double>>& knotsArray,
	std::vector<gp_Pnt>& boundaryPoints,
	std::vector<gp_Pnt>& interPoints,
	Standard_Integer isoCount,
	std::vector<TopoDS_Edge>& TangentArray,
	const std::vector<Handle(Geom_BSplineSurface)>& surfaceArr)
{
	Standard_Integer degree = isoCurvesArray_New[0]->Degree();
	for (int i = 0; i < isoCurvesArray_New.size(); i++)
	{
		auto curve = isoCurvesArray_New[i];

		gp_Pnt startPoint = curve->StartPoint();
		gp_Pnt endPoint = curve->EndPoint();
		boundaryPoints.push_back(startPoint);
		boundaryPoints.push_back(endPoint);
		std::vector<gp_Pnt> intersectionPoints;

		// 遍历相对方向的等参线，计算交点
		for (int j = 0; j < oppsiteISOcurvesArray_New.size(); j++)
		{
			Handle(Geom_BSplineCurve) oppositeCurve = oppsiteISOcurvesArray_New[j];
			GeomAPI_ExtremaCurveCurve extrema(curve, oppositeCurve);
			if (extrema.NbExtrema() > 0)
			{
				gp_Pnt P1, P2;
				extrema.NearestPoints(P1, P2);
				std::pair<double, double> weights = processPoints(P1, P2, isoInterpolatePoints[i], oppsiteInterpolatePoints[j]);
				// 获取权重值
				double w1 = weights.first;
				double w2 = weights.second;
				gp_Pnt midPoint;
				double x = (P1.X() * w1 + P2.X() * w2);
				double y = (P1.Y() * w1 + P2.Y() * w2);
				double z = (P1.Z() * w1 + P2.Z() * w2);
				midPoint.SetX(x); midPoint.SetY(y);midPoint.SetZ(z);
				intersectionPoints.push_back(midPoint);
				interPoints.push_back(midPoint);
			}
		}

		std::sort(intersectionPoints.begin(), intersectionPoints.end(),
			[&startPoint](const gp_Pnt& p1, const gp_Pnt& p2)
			{
				return p1.Distance(startPoint) < p2.Distance(startPoint);
			});

		intersectionPoints.insert(intersectionPoints.begin(), startPoint);
		intersectionPoints.push_back(endPoint);

		for (int j = 1; j < intersectionPoints.size() - 1; j++)
		{
			if (intersectionPoints[j].Distance(intersectionPoints[j - 1]) < startPoint.Distance(endPoint) / (isoCount * 2) ||
				intersectionPoints[j].Distance(intersectionPoints[j + 1]) < startPoint.Distance(endPoint) / (isoCount * 2))
			{
				intersectionPoints.erase(intersectionPoints.begin() + j);
				--j;
			}
		}

		// 计算切向约束
	// U_Tangent指在该点的U向切向，V_Tangent指在该点的V向切向
		gp_Vec U_StartTangent, V_StartTangent;
		gp_Vec U_EndTangent, V_EndTangent;
		// 计算切向和指定方向的角度，取小的
		double cosAngleStartU, cosAngleStartV, cosAngleEndU, cosAngleEndV;
		gp_Vec startDirection = (intersectionPoints[1].XYZ() - intersectionPoints[0].XYZ()).Normalized(); // 切线指向
		gp_Vec endDirection = (intersectionPoints[intersectionPoints.size() - 1].XYZ() - intersectionPoints[intersectionPoints.size() - 2].XYZ()).Normalized(); // 切线指向

		// startPoint切线和endPoint的切线
		gp_Vec FirstD1, LastD1;
		if (!surfaceArr[0].IsNull())
		{
			GeomAPI_ProjectPointOnSurf projector(intersectionPoints.front(), surfaceArr[0]);
			if (projector.NbPoints() > 0)
			{
				gp_Pnt closestPoint = projector.NearestPoint();
				double uParam, vParam;
				projector.LowerDistanceParameters(uParam, vParam);

				// 获取U和V方向的偏导数（切向量）
				surfaceArr[0]->D1(uParam, vParam, closestPoint, U_StartTangent, V_StartTangent);

				// 计算法向量（U和V偏导数的叉积）
				gp_Vec normalVector = U_StartTangent.Crossed(V_StartTangent);
				normalVector = normalVector.Normalized();
				// 确保法向量与startDirection夹角为锐角
				if (startDirection.Dot(normalVector) < 0)
				{
					normalVector.Reverse(); // 反向法向量
				}

				// 计算startDirection在法向量上的投影分量
				double projectionMagnitude = startDirection.Dot(normalVector) / normalVector.Magnitude();
				gp_Vec projection = normalVector * projectionMagnitude; // 计算投影向量

				// 计算startDirection在平面上的投影（去掉法向量的分量）
				gp_Vec projectedDirection = startDirection - projection;

				// 归一化投影方向
				projectedDirection.Normalize();

				FirstD1 = projectedDirection; // 将投影后的方向作为切线
			}
		}
		else
		{
			// 计算前三个点的切向
			if (intersectionPoints.size() >= 3)
			{
				std::vector<gp_Pnt> points(intersectionPoints.begin(), intersectionPoints.begin() + 3);
				FirstD1 = CalTangent(points, 1).Normalized();
			}
		}
		FirstD1.Multiply(CalPointsChordLen(intersectionPoints) / FirstD1.Magnitude());
		TangentArray.push_back(BRepBuilderAPI_MakeEdge(intersectionPoints.front(), intersectionPoints.front().Translated(FirstD1 * 0.1)).Edge());

		if (!surfaceArr[1].IsNull())
		{
			GeomAPI_ProjectPointOnSurf projector(intersectionPoints.back(), surfaceArr[1]);
			if (projector.NbPoints() > 0)
			{
				gp_Pnt closestPoint = projector.NearestPoint();
				double uParam, vParam;
				projector.LowerDistanceParameters(uParam, vParam);

				// 获取U和V方向的偏导数（切向量）
				surfaceArr[1]->D1(uParam, vParam, closestPoint, U_EndTangent, V_EndTangent);

				// 计算法向量（U和V偏导数的叉积）
				gp_Vec normalVector = U_EndTangent.Crossed(V_EndTangent);
				normalVector = normalVector.Normalized();
				// 确保法向量与startDirection夹角为锐角
				if (endDirection.Dot(normalVector) < 0)
				{
					normalVector.Reverse(); // 反向法向量
				}
				// 计算endDirection在法向量上的投影分量
				double projectionMagnitude = endDirection.Dot(normalVector) / normalVector.Magnitude();
				gp_Vec projection = normalVector * projectionMagnitude; // 计算投影向量

				// 计算endDirection在平面上的投影（去掉法向量的分量）
				gp_Vec projectedDirection = endDirection - projection;

				// 归一化投影方向
				projectedDirection.Normalize();

				LastD1 = projectedDirection; // 将投影后的方向作为切线
			}
		}
		else
		{
			// 计算前三个点的切向
			if (intersectionPoints.size() >= 3)
			{
				std::vector<gp_Pnt> points(intersectionPoints.rbegin(), intersectionPoints.rbegin() + 3);
				LastD1 = CalTangent(points, 1).Normalized();
				LastD1.Reverse();
			}
		}


		LastD1.Multiply(CalPointsChordLen(intersectionPoints) / LastD1.Magnitude());
		TangentArray.push_back(BRepBuilderAPI_MakeEdge(intersectionPoints.back(),intersectionPoints.back().Translated(LastD1 * 0.1)).Edge());
		std::vector<double> params = ComputeUniformParam(intersectionPoints.size(), 0., 1.);
		std::vector<double> tempKnots = KnotGernerationByParams(params, 5, degree);
		std::vector<double> insertKnots;

		Handle(Geom_BSplineCurve) aBSplineCurve = IterateApproximate(insertKnots, intersectionPoints, FirstD1, LastD1, params, tempKnots, degree, 50, 0.01);
		knotsArray.push_back(GetKnotsSequence(aBSplineCurve));
		isoCurvesArray_Final.emplace_back(aBSplineCurve);

		gp_Vec TestFirstD1, TestLastD1;
		gp_Pnt pnt;
		aBSplineCurve->D1(aBSplineCurve->FirstParameter(),pnt, TestFirstD1);
		aBSplineCurve->D1(aBSplineCurve->LastParameter(),pnt, TestLastD1);
		auto areVectorsEqual = [](const gp_Vec& vec1, const gp_Vec& vec2, double tol = 1e-6)
			{
			return std::abs(vec1.X() - vec2.X()) <= tol &&
				std::abs(vec1.Y() - vec2.Y()) <= tol &&
				std::abs(vec1.Z() - vec2.Z()) <= tol;
			};
		if (!areVectorsEqual(TestFirstD1, FirstD1))
		{
			std::cout << "TestFirstD1 与 FirstD1 不相等" << std::endl;
		}
		if (!areVectorsEqual(TestLastD1, LastD1))
		{
			std::cout << "TestLastD1 与 LastD1 不相等" << std::endl;
		}


	}
}

// 计算最近的切向表面
Handle(Geom_BSplineSurface) FindClosestSurface(
	gp_Pnt point,
	std::vector<Handle(Geom_BSplineSurface)>& surfaceArr,
	double threshold)
{
	double minDistance = std::numeric_limits<double>::max();
	int closestSurfaceIndex = -1;

	for (size_t i = 0; i < surfaceArr.size(); ++i)
	{
		const auto& surface = surfaceArr[i];
		GeomAPI_ProjectPointOnSurf projector(point, surface);
		double distance = 0.0;

		if (projector.NbPoints() > 0)
		{
			gp_Pnt closestPoint = projector.NearestPoint();
			distance = point.Distance(closestPoint);
		}

		if (distance < minDistance)
		{
			minDistance = distance;
			closestSurfaceIndex = static_cast<int>(i);
		}
	}

	if (closestSurfaceIndex != -1 && minDistance < threshold)
	{
		return surfaceArr[closestSurfaceIndex];
	}
	return nullptr; // 若没有找到有效的表面，返回空指针
}

void SurfaceModelingTool::CreateFinalISOCurves(
	const std::vector<Handle(Geom_BSplineCurve)>& uISOcurvesArray_New,
	const std::vector<Handle(Geom_BSplineCurve)>& vISOcurvesArray_New,
	std::vector<Handle(Geom_BSplineCurve)>& uISOcurvesArray_Final,
	std::vector<Handle(Geom_BSplineCurve)>& vISOcurvesArray_Final,
	std::vector<std::vector<gp_Pnt>>& uInterpolatePoints,
	std::vector<std::vector<gp_Pnt>>& vInterpolatePoints,
	std::vector<std::vector<double>>& uKnots,
	std::vector<std::vector<double>>& vKnots,
	std::vector<gp_Pnt>& boundaryPoints,
	std::vector<gp_Pnt>& interPoints,
	Standard_Integer isoCount,
	std::vector<TopoDS_Edge>& TangentArray,
	std::vector<Handle(Geom_BSplineSurface)>& surfaceArr)
{
	auto uCurve = uISOcurvesArray_New[0];
	Standard_Integer degree = uCurve->Degree();
	UniformCurve(uCurve);
	uKnots.push_back(GetKnotsSequence(uCurve));

	auto vCurve = vISOcurvesArray_New[0];
	UniformCurve(vCurve);
	vKnots.push_back(GetKnotsSequence(vCurve));

	// Surfacce[0] 代表 startpoint所在的区间相邻的面
	// Surfacce[1] 代表 endpoint 所在的区间相邻的面
	std::vector<Handle(Geom_BSplineSurface)> uTangentSurface(2);
	std::vector<Handle(Geom_BSplineSurface)> vTangentSurface(2);

	double minDistance = INT_MAX;  // 初始为最大值
	int closestSurfaceIndex = -1;  // 初始化为无效索引

	gp_Pnt startPoint = uISOcurvesArray_New[uISOcurvesArray_New.size() / 2]->StartPoint();
	gp_Pnt endPoint = uISOcurvesArray_New[uISOcurvesArray_New.size() / 2]->EndPoint();

	if (startPoint.Distance(endPoint) > 1e-6)
	{
		// 计算 uTangentSurface[0] 和 uTangentSurface[1]
		uTangentSurface[0] = FindClosestSurface(startPoint, surfaceArr, startPoint.Distance(endPoint) / 1000.0);
		uTangentSurface[1] = FindClosestSurface(endPoint, surfaceArr, startPoint.Distance(endPoint) / 1000.0);
	}


	// 使用 vISOcurvesArray_New 的中间曲线来获取新的起点和终点
	gp_Pnt newStartPoint = vISOcurvesArray_New[vISOcurvesArray_New.size() / 2]->StartPoint();
	gp_Pnt newEndPoint = vISOcurvesArray_New[vISOcurvesArray_New.size() / 2]->EndPoint();

	if (newStartPoint.Distance(newEndPoint) > 1e-6)
	{
		// 计算 vTangentSurface[0] 和 vTangentSurface[1]
		vTangentSurface[0] = FindClosestSurface(newStartPoint, surfaceArr, newStartPoint.Distance(newEndPoint) / 1000);
		vTangentSurface[1] = FindClosestSurface(newEndPoint, surfaceArr, newStartPoint.Distance(newEndPoint) / 1000);
	}
	ProcessISOCurvesWithTangent(uISOcurvesArray_New, vISOcurvesArray_New, uInterpolatePoints, vInterpolatePoints, uISOcurvesArray_Final, uKnots, boundaryPoints, interPoints, isoCount, TangentArray, uTangentSurface);
	ProcessISOCurvesWithTangent(vISOcurvesArray_New, uISOcurvesArray_New, vInterpolatePoints, uInterpolatePoints, vISOcurvesArray_Final, vKnots, boundaryPoints, interPoints, isoCount, TangentArray, vTangentSurface);

	uCurve = uISOcurvesArray_New.back();
	UniformCurve(uCurve);
	uKnots.push_back(GetKnotsSequence(uCurve));

	vCurve = vISOcurvesArray_New.back();
	UniformCurve(vCurve);
	vKnots.push_back(GetKnotsSequence(vCurve));
}
void SurfaceModelingTool::LoadBSplineCurves(const std::string& filePath, std::vector<Handle(Geom_BSplineCurve)>& curveArray)
{
	// 获取文件后缀
	std::string extension = filePath.substr(filePath.find_last_of('.') + 1);

	TopoDS_Shape boundary;
	if (extension == "brep") 
	{
		// 初始化边界Shape
		BRep_Builder B1;
		// 从文件读取BRep数据
		BRepTools::Read(boundary, filePath.c_str(), B1);
	}
	else if(extension == "step" || extension == "stp")
	{
		// 创建 STEP 文件读取器
		STEPControl_Reader reader;
		IFSelect_ReturnStatus status = reader.ReadFile(filePath.c_str());

		if (status == IFSelect_ReturnStatus::IFSelect_RetDone) 
		{
			// 传输读取的数据
			reader.TransferRoots();
			boundary = reader.OneShape();
		}
	}
	else if (extension == "igs" || extension == "iges")
	{
		// 创建 IGES 文件读取器
		IGESControl_Reader igesReader;
		IFSelect_ReturnStatus status = igesReader.ReadFile(filePath.c_str());

		if (status == IFSelect_ReturnStatus::IFSelect_RetDone)
		{
			// 传输读取的数据
			igesReader.TransferRoots();
			boundary = igesReader.OneShape();
		}
	}

	// 遍历Shape中的边
	TopExp_Explorer explorer(boundary, TopAbs_EDGE);
	for (; explorer.More(); explorer.Next()) 
	{
		TopoDS_Edge edge = TopoDS::Edge(explorer.Current());

		// 获取边的几何表示
		TopLoc_Location loc;
		Standard_Real first, last;
		Handle(Geom_Curve) gcurve = BRep_Tool::Curve(edge, loc, first, last);
		gcurve = Handle(Geom_Curve)::DownCast(gcurve->Copy());

		// 检查曲线类型
		if (gcurve->DynamicType() == STANDARD_TYPE(Geom_Line)) 
		{
			// 如果是直线，转换为BSpline
			Handle(Geom_TrimmedCurve) aTrimmedLine = new Geom_TrimmedCurve(gcurve, first, last);
			Handle(Geom_BSplineCurve) aGeom_BSplineCurve = GeomConvert::CurveToBSplineCurve(aTrimmedLine);
			if (!aGeom_BSplineCurve.IsNull() && aGeom_BSplineCurve->IsKind(STANDARD_TYPE(Geom_BSplineCurve)))
			{
				curveArray.push_back(aGeom_BSplineCurve);
			}
		}
		else if (gcurve->DynamicType() == STANDARD_TYPE(Geom_BSplineCurve)) 
		{
			// 如果已经是BSpline，直接处理
			Handle(Geom_BSplineCurve) aGeom_BSplineCurve = Handle(Geom_BSplineCurve)::DownCast(gcurve);
			if (!aGeom_BSplineCurve.IsNull()) {
				aGeom_BSplineCurve->Segment(first, last);
				if (!aGeom_BSplineCurve.IsNull() && aGeom_BSplineCurve->IsKind(STANDARD_TYPE(Geom_BSplineCurve))) 
				{
					curveArray.push_back(aGeom_BSplineCurve);
				}
			}
		}
	}
}
void SurfaceModelingTool::LoadBSplineSurfaces(const std::string& filePath, std::vector<Handle(Geom_BSplineSurface)>& surfaceArray)
{
	if (!std::filesystem::exists(filePath))
	{
		return;
	}
	try
	{
		// 获取文件扩展名
		std::filesystem::path path(filePath);
		std::string extension = path.extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

		TopoDS_Shape shape;

		// 根据文件类型选择相应的读取器
		if (extension == ".stp" || extension == ".step")
		{
			STEPControl_Reader reader;
			IFSelect_ReturnStatus status = reader.ReadFile(filePath.c_str());

			if (status != IFSelect_RetDone)
			{
				throw Standard_Failure("Failed to read STEP file");
			}

			reader.TransferRoots();
			shape = reader.OneShape();
		}
		else if (extension == ".igs" || extension == ".iges")
		{
			IGESControl_Reader reader;
			IFSelect_ReturnStatus status = reader.ReadFile(filePath.c_str());

			if (status != IFSelect_RetDone)
			{
				throw Standard_Failure("Failed to read IGES file");
			}

			reader.TransferRoots();
			shape = reader.OneShape();
		}
		else if (extension == ".brep" || extension == ".brp")
		{
			BRep_Builder builder;
			Standard_Boolean result = BRepTools::Read(shape, filePath.c_str(), builder);

			if (!result)
			{
				throw Standard_Failure("Failed to read BREP file");
			}
		}
		else
		{
			throw Standard_Failure("Unsupported file format");
		}

		if (shape.IsNull())
		{
			throw Standard_Failure("No valid shape found in file");
		}

		// 遍历所有面并提取B样条曲面
		TopExp_Explorer explorer(shape, TopAbs_FACE);
		while (explorer.More())
		{
			const TopoDS_Face& face = TopoDS::Face(explorer.Current());
			Handle(Geom_Surface) surface = BRep_Tool::Surface(face);

			if (!surface.IsNull())
			{
				Handle(Geom_BSplineSurface) bsplineSurface;

				// 如果已经是BSplineSurface
				if (surface->IsKind(STANDARD_TYPE(Geom_BSplineSurface)))
				{
					bsplineSurface = Handle(Geom_BSplineSurface)::DownCast(surface);
				}
				// 尝试转换为BSplineSurface
				else
				{
					try
					{
						bsplineSurface = GeomConvert::SurfaceToBSplineSurface(surface);
					}
					catch (const Standard_Failure& e)
					{
						std::cerr << "Warning: Failed to convert surface to BSpline: "
							<< e.GetMessageString() << std::endl;
						continue;
					}
				}

				if (!bsplineSurface.IsNull())
				{
					surfaceArray.push_back(bsplineSurface);
				}
			}
			explorer.Next();
		}

		if (surfaceArray.empty())
		{
			std::cout << "Warning: No B-Spline surfaces found in file." << std::endl;
		}
		else
		{
			std::cout << "Successfully loaded " << surfaceArray.size()
				<< " B-Spline surface(s) from " << path.filename().string() << std::endl;
		}
	}
	catch (const Standard_Failure& e)
	{
		std::cerr << "Error: " << e.GetMessageString() << std::endl;
		throw;
	}
}
void SurfaceModelingTool::GetISOCurveWithNormal(const Handle(Geom_BSplineSurface)& surfacecoons, std::vector<Handle(Geom_BSplineCurve)>& uISOcurvesArray_Initial, std::vector<Handle(Geom_BSplineCurve)>& vISOcurvesArray_Initial, std::vector<gp_Vec>& normalsOfUISOLines, std::vector<gp_Vec>& normalsOfVISOLines, int numIsoCurves)
{
	auto IsPointOnSurface = [](const gp_Pnt& point, const Handle(Geom_Surface)& surface)
	{
		GeomAPI_ProjectPointOnSurf proj(point, surface);

		if (proj.NbPoints() > 0) 
		{
			gp_Pnt closestPoint = proj.NearestPoint();

			return point.Distance(closestPoint) < 1e-6;
		}

		return false;
	};

	auto GetClosestPointOnSurface = [](const gp_Pnt& point, const Handle(Geom_Surface)& surface)
	{
		GeomAPI_ProjectPointOnSurf proj(point, surface);

		if (proj.NbPoints() > 0)
		{
			return proj.NearestPoint(); // 返回最近点
		}

		return point;
	};

	std::vector<std::pair<gp_Vec, gp_Vec>> tangentOfUISOLines;
	std::vector<std::pair<gp_Vec, gp_Vec>> tangentOfVISOLines;
	const int numSamplePoints = 10;
	for (int i = 1; i < numIsoCurves; i++)
	{
		std::vector<gp_Vec> normalsU;
		std::vector<gp_Vec> normalsV;

		Handle(Geom_BSplineCurve) aUGeom_BSplineCurve = Handle(Geom_BSplineCurve)::DownCast(surfacecoons->UIso(((double)i / numIsoCurves) 
			* (surfacecoons->UKnot(surfacecoons->LastUKnotIndex()) - surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())) + surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())));
		uISOcurvesArray_Initial.emplace_back(aUGeom_BSplineCurve);

		// 获取u方向BSpline曲线的参数范围
		double uStart = aUGeom_BSplineCurve->FirstParameter();
		double uEnd = aUGeom_BSplineCurve->LastParameter();

		gp_Vec startTangent, endTangent;
		for (int j = 0; j < numSamplePoints; j++)
		{
			double t = uStart + j * (uEnd - uStart) / (numSamplePoints - 1); // 在实际参数范围内均匀取点
			gp_Pnt p1 = aUGeom_BSplineCurve->Value(t);
			gp_Vec DXu, DXv, N;

			if (IsPointOnSurface(p1, surfacecoons)) 
			{
				surfacecoons->D1(((double)i / numIsoCurves) * (surfacecoons->UKnot(surfacecoons->LastUKnotIndex()) - surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())) + surfacecoons->UKnot(surfacecoons->FirstUKnotIndex()), 
					((double)j / numSamplePoints) * (surfacecoons->VKnot(surfacecoons->LastVKnotIndex()) - surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())) + surfacecoons->VKnot(surfacecoons->FirstVKnotIndex()), p1, DXu, DXv);
			}
			else
			{
				gp_Pnt closestPoint = GetClosestPointOnSurface(p1, surfacecoons);
				surfacecoons->D1(((double)i / numIsoCurves) * (surfacecoons->UKnot(surfacecoons->LastUKnotIndex()) - surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())) + surfacecoons->UKnot(surfacecoons->FirstUKnotIndex()),
					((double)j / numSamplePoints) * (surfacecoons->VKnot(surfacecoons->LastVKnotIndex()) - surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())) + surfacecoons->VKnot(surfacecoons->FirstVKnotIndex()), closestPoint, DXu, DXv);
			}

			DXu.Cross(DXv);
			N = DXu.Normalized();
			normalsU.push_back(N);
		}

		// 平均法向量
		gp_Vec avgNormalU = std::accumulate(normalsU.begin(), normalsU.end(), gp_Vec()) / normalsU.size();
		normalsOfUISOLines.emplace_back(avgNormalU);

		// 处理v方向的Iso曲线
		Handle(Geom_BSplineCurve) aVGeom_BSplineCurve = Handle(Geom_BSplineCurve)::DownCast(surfacecoons->VIso(((double)i / numIsoCurves)
			* (surfacecoons->VKnot(surfacecoons->LastVKnotIndex()) - surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())) + surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())));
		vISOcurvesArray_Initial.emplace_back(aVGeom_BSplineCurve);

		double vStart = aVGeom_BSplineCurve->FirstParameter();
		double vEnd = aVGeom_BSplineCurve->LastParameter();
		for (int j = 0; j < numSamplePoints; j++)
		{
			double t = vStart + j * (vEnd - vStart) / (numSamplePoints - 1);
			gp_Pnt p1 = aVGeom_BSplineCurve->Value(t);
			gp_Vec DXu, DXv, N;
			gp_Vec startTangent, endTangent;

			// 计算法向量
			if (IsPointOnSurface(p1, surfacecoons))
			{
				surfacecoons->D1(((double)j / numSamplePoints) * (surfacecoons->UKnot(surfacecoons->LastUKnotIndex()) - surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())) + surfacecoons->UKnot(surfacecoons->FirstUKnotIndex()), 
					((double)i / numIsoCurves) * (surfacecoons->VKnot(surfacecoons->LastVKnotIndex()) - surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())) + surfacecoons->VKnot(surfacecoons->FirstVKnotIndex()), p1, DXu, DXv);
			}
			else
			{
				gp_Pnt closestPoint = GetClosestPointOnSurface(p1, surfacecoons);
				surfacecoons->D1(((double)j / numSamplePoints) * (surfacecoons->UKnot(surfacecoons->LastUKnotIndex()) - surfacecoons->UKnot(surfacecoons->FirstUKnotIndex())) + surfacecoons->UKnot(surfacecoons->FirstUKnotIndex()), 
					((double)i / numIsoCurves) * (surfacecoons->VKnot(surfacecoons->LastVKnotIndex()) - surfacecoons->VKnot(surfacecoons->FirstVKnotIndex())) + surfacecoons->VKnot(surfacecoons->FirstVKnotIndex()), closestPoint, DXu, DXv);
			}

			//gp_Vec direction = aVGeom_BSplineCurve->EndPoint().XYZ() - aVGeom_BSplineCurve->StartPoint().XYZ(); // 得到方向向量
			//direction.Normalize();
			//if (j == 0)
			//{
			//	startTangent = DXv.Normalized();
			//	if (startTangent.Dot(direction) < 0)
			//	{
			//		startTangent = -startTangent; // 反转切向量的方向
			//	}
			//}
			//if (j == numSamplePoints - 1)
			//{
			//	endTangent = DXv.Normalized();
			//	if (endTangent.Dot(direction) < 0)
			//	{
			//		endTangent = -endTangent; // 反转切向量的方向
			//	}
			//}
			//tangentOfVISOLines.push_back(std::make_pair(startTangent.Normalized(), endTangent.Normalized()));

			DXu.Cross(DXv);
			N = DXu.Normalized();
			normalsV.push_back(N);
		}

		// 平均法向量
		gp_Vec avgNormalV = std::accumulate(normalsV.begin(), normalsV.end(), gp_Vec()) / normalsV.size();
		normalsOfVISOLines.emplace_back(avgNormalV);

	}
}

void SurfaceModelingTool::ApproximateBoundaryCurves(std::vector<Handle(Geom_BSplineCurve)>& curves, int samplingNum)
{
	for (auto& curve : curves) {
		TColStd_Array1OfReal curveKnots(1, curve->NbKnots());
		curve->Knots(curveKnots);

		// 重新参数化曲线的节点
		if (!(curveKnots(curveKnots.Lower()) == 0 && curveKnots(curveKnots.Upper()) == 1)) {
			BSplCLib::Reparametrize(0, 1, curveKnots);
			curve->SetKnots(curveKnots);
		}

		// 采样点与参数生成
		std::vector<gp_Pnt> samplingPnts;
		std::vector<Standard_Real> samplingParams;
		Standard_Real vMin = curve->FirstParameter();
		Standard_Real vMax = curve->LastParameter();

		for (int j = 1; j <= samplingNum; j++) {
			Standard_Real param = vMin + (vMax - vMin) * (j - 1) / (samplingNum - 1);
			gp_Pnt pnt = curve->Value(param);
			samplingParams.push_back(param);
			samplingPnts.push_back(pnt);
		}

		// 初始化节点并进行拟合
		std::vector<Standard_Real> init_knots = KnotGernerationByParams(samplingParams, 6, 3);
		std::vector<Standard_Real> insertKnots;
		curve = IterateApproximate(insertKnots, samplingPnts, samplingParams, init_knots, 3, 10, 1);
	}
}

void SurfaceModelingTool::UpdateFinalCurves(const std::vector<Handle(Geom_BSplineCurve)>& aBoundarycurveArray, 
	std::vector<Handle(Geom_BSplineCurve)>& uISOcurvesArray_Final, 
	std::vector<Handle(Geom_BSplineCurve)>& vISOcurvesArray_Final)
{
	Handle(Geom_BSplineCurve) uCurve = uISOcurvesArray_Final[0];
	Handle(Geom_BSplineCurve) vCurve = vISOcurvesArray_Final[0];
	std::vector<Handle(Geom_BSplineCurve)> uBoundaryCurve(2), vBoundaryCurve(2);

	bool isUBoundaryFirst = ComputeCurveCurveDistance(aBoundarycurveArray[0], uCurve) >
		ComputeCurveCurveDistance(aBoundarycurveArray[1], uCurve);

	uBoundaryCurve[0] = isUBoundaryFirst ? aBoundarycurveArray[0] : aBoundarycurveArray[1];
	uBoundaryCurve[1] = isUBoundaryFirst ? aBoundarycurveArray[2] : aBoundarycurveArray[3];
	vBoundaryCurve[0] = isUBoundaryFirst ? aBoundarycurveArray[1] : aBoundarycurveArray[0];
	vBoundaryCurve[1] = isUBoundaryFirst ? aBoundarycurveArray[3] : aBoundarycurveArray[2];

	gp_Pnt uCurveSamplePoint = ComputeAverageSamplePoint(uCurve, 10);
	bool isUFirstCloser = uCurveSamplePoint.Distance(ComputeAverageSamplePoint(uBoundaryCurve[0], 10)) <
		uCurveSamplePoint.Distance(ComputeAverageSamplePoint(uBoundaryCurve[1], 10));

	uISOcurvesArray_Final.insert(uISOcurvesArray_Final.begin(), isUFirstCloser ? uBoundaryCurve[0] : uBoundaryCurve[1]);
	uISOcurvesArray_Final.insert(uISOcurvesArray_Final.end(), isUFirstCloser ? uBoundaryCurve[1] : uBoundaryCurve[0]);

	gp_Pnt vCurveSamplePoint = ComputeAverageSamplePoint(vCurve, 10);
	bool isVFirstCloser = vCurveSamplePoint.Distance(ComputeAverageSamplePoint(vBoundaryCurve[0], 10)) <
		vCurveSamplePoint.Distance(ComputeAverageSamplePoint(vBoundaryCurve[1], 10));

	vISOcurvesArray_Final.insert(vISOcurvesArray_Final.begin(), isVFirstCloser ? vBoundaryCurve[0] : vBoundaryCurve[1]);
	vISOcurvesArray_Final.insert(vISOcurvesArray_Final.end(), isVFirstCloser ? vBoundaryCurve[1] : vBoundaryCurve[0]);

	gp_Pnt origin(0, 0, 0);
	auto minDistanceToPoint = [&origin](const Handle(Geom_BSplineCurve)& curve) 
	{
		return std::min(curve->StartPoint().Distance(origin), curve->EndPoint().Distance(origin));
	};

	// 如果最后一条曲线比第一条曲线距离原点更近，反转 uISOcurvesArray_Final
	if (minDistanceToPoint(uISOcurvesArray_Final[uISOcurvesArray_Final.size() - 1]) < minDistanceToPoint(uISOcurvesArray_Final[0])) 
	{
		std::reverse(uISOcurvesArray_Final.begin(), uISOcurvesArray_Final.end());
	}
	// 对 vISO 曲线进行同样的处理
	if (minDistanceToPoint(vISOcurvesArray_Final[vISOcurvesArray_Final.size() - 1]) < minDistanceToPoint(vISOcurvesArray_Final[0]))
	{
		std::reverse(vISOcurvesArray_Final.begin(), vISOcurvesArray_Final.end());
	}

}
