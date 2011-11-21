#include "Core.h"
#include "UnCore.h"

#include "UnObject.h"
#include "UnMaterial.h"

#include "SkeletalMesh.h"

#include "Exporters.h"						// for GetExportPath()


// MD5 uses right-hand coordinates, but unreal uses left-hand.
// When importing PSK into UnrealEd, it mirrors model.
// Here we performing reverse transformation.
#define MIRROR_MESH			1


// function is similar to part of CSkelMeshInstance::SetMesh() and Rune mesh loader
static void BuildSkeleton(TArray<CCoords> &Coords, const TArray<CSkelMeshBone> &Bones)
{
	guard(BuildSkeleton);

	int numBones = Bones.Num();
	Coords.Empty(numBones);
	Coords.Add(numBones);

	for (int i = 0; i < numBones; i++)
	{
		const CSkelMeshBone &B = Bones[i];
		CCoords &BC = Coords[i];
		// compute reference bone coords
		CVec3 BP;
		CQuat BO;
		// get default pose
		BP = B.Position;
		BO = B.Orientation;
#if MIRROR_MESH
		BP[1] *= -1;							// y
		BO.y  *= -1;
		BO.w  *= -1;
#endif
		if (!i) BO.Conjugate();					// root bone

		BC.origin = BP;
		BO.ToAxis(BC.axis);
		// move bone position to global coordinate space
		if (i)	// do not rotate root bone
		{
			assert(B.ParentIndex < i);
			Coords[B.ParentIndex].UnTransformCoords(BC, BC);
		}
#if 0
	//!!
if (i == 32)
{
	appNotify("Bone %d (%8.3f %8.3f %8.3f) - (%8.3f %8.3f %8.3f %8.3f)", i, VECTOR_ARG(BP), QUAT_ARG(BO));
#define C BC
	appNotify("REF   : o=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.origin ));
	appNotify("        0=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[0]));
	appNotify("        1=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[1]));
	appNotify("        2=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[2]));
#undef C
}
#endif
	}

	unguard;
}

struct VertInfluence
{
	int		Vert;
	int		Bone;
	float	Weight;
};

struct VertInfluences
{
	TArray<VertInfluence> Inf;
};

void ExportMd5Mesh(const CSkeletalMesh *Mesh, FArchive &Ar)
{
	guard(ExportMd5Mesh);

	int i;

	UObject *OriginalMesh = Mesh->OriginalMesh;
	if (!Mesh->Lods.Num())
	{
		appNotify("Mesh %s has 0 lods", OriginalMesh->Name);
		return;
	}
	const CSkelMeshLod &Lod = Mesh->Lods[0];

	Ar.Printf(
		"MD5Version 10\n"
		"commandline \"Created with UE Viewer\"\n"
		"\n"
		"numJoints %d\n"
		"numMeshes %d\n"
		"\n",
		Mesh->RefSkeleton.Num(),
		Lod.Sections.Num()
	);

	// compute skeleton
	TArray<CCoords> BoneCoords;
	BuildSkeleton(BoneCoords, Mesh->RefSkeleton);

	// write joints
	Ar.Printf("joints {\n");
	for (i = 0; i < Mesh->RefSkeleton.Num(); i++)
	{
		const CSkelMeshBone &B = Mesh->RefSkeleton[i];

		const CCoords &BC = BoneCoords[i];
		CVec3 BP;
		CQuat BO;
		BP = BC.origin;
		BO.FromAxis(BC.axis);
		if (BO.w < 0) BO.Negate();				// W-component of quaternion will be removed ...

		Ar.Printf(
			"\t\"%s\"\t%d ( %f %f %f ) ( %.10f %.10f %.10f )\n",
			*B.Name, (i == 0) ? -1 : B.ParentIndex,
			VECTOR_ARG(BP),
			BO.x, BO.y, BO.z
		);
#if 0
	//!!
if (i == 32 || i == 34)
{
	CCoords BC;
	BC.origin = BP;
	BO.ToAxis(BC.axis);
	appNotify("Bone %d (%8.3f %8.3f %8.3f) - (%8.3f %8.3f %8.3f %8.3f)", i, VECTOR_ARG(BP), QUAT_ARG(BO));
#define C BC
	appNotify("INV   : o=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.origin ));
	appNotify("        0=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[0]));
	appNotify("        1=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[1]));
	appNotify("        2=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[2]));
#undef C
//	BO.Negate();
	BO.w *= -1;
	BO.ToAxis(BC.axis);
#define C BC
	appNotify("INV2  : o=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.origin ));
	appNotify("        0=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[0]));
	appNotify("        1=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[1]));
	appNotify("        2=%8.3f %8.3f %8.3f",    VECTOR_ARG(C.axis[2]));
#undef C
}
#endif
	}
	Ar.Printf("}\n\n");

	// collect weights information
	TArray<VertInfluences> Weights;				// Point -> Influences
	Weights.Add(Lod.NumVerts);
	for (i = 0; i < Lod.NumVerts; i++)
	{
		const CSkelMeshVertex &V = Lod.Verts[i];
		for (int j = 0; j < NUM_INFLUENCES; j++)
		{
			if (V.Bone[j] < 0) break;
			VertInfluence *I = new (Weights[i].Inf) VertInfluence;
			I->Bone   = V.Bone[j];
			I->Weight = V.Weight[j];
		}
	}

	int numIndices  = Lod.Indices.Num();
	int numFaces    = numIndices / 3;
	CIndexBuffer::IndexAccessor_t Index = Lod.Indices.GetAccessor();

	// write meshes
	// we are using some terms here:
	// - "mesh vertex" is a vertex in Lod.Verts[] array, global for whole mesh
	// - "surcace vertex" is a vertex from the mesh stripped to only one (current) section
	for (int m = 0; m < Lod.Sections.Num(); m++)
	{
		const CSkelMeshSection &Sec = Lod.Sections[m];

		TArray<int>  MeshVerts;					// surface vertex -> mesh vertex
		TArray<int>  BackWedge;					// mesh vertex -> surface vertex
		TArray<bool> UsedVerts;					// mesh vertex -> surface: used of not
		TArray<int>  MeshWeights;				// mesh vertex -> weight index
		MeshVerts.Empty(Lod.NumVerts);
		UsedVerts.Add(Lod.NumVerts);
		BackWedge.Add(Lod.NumVerts);
		MeshWeights.Add(Lod.NumVerts);

		// find verts and triangles for current material
		for (i = 0; i < Sec.NumFaces * 3; i++)
		{
			int idx = Index(Lod.Indices, i + Sec.FirstIndex);

			if (UsedVerts[idx]) continue;		// vertex is already used in previous triangle
			UsedVerts[idx] = true;
			int locWedge = MeshVerts.AddItem(idx);
			BackWedge[idx] = locWedge;
		}

		// find influences
		int WeightIndex = 0;
		for (i = 0; i < Lod.NumVerts; i++)
		{
			if (!UsedVerts[i]) continue;
			MeshWeights[i] = WeightIndex;
			WeightIndex += Weights[i].Inf.Num();
		}

		// mesh header
		const UUnrealMaterial *Tex = Sec.Material;
		if (Tex)
		{
			Ar.Printf(
				"mesh {\n"
				"\tshader \"%s\"\n\n",
				Tex->Name
			);
			ExportObject(Tex);
		}
		else
		{
			Ar.Printf(
				"mesh {\n"
				"\tshader \"material_%d\"\n\n",
				m
			);
		}
		// verts
		Ar.Printf("\tnumverts %d\n", MeshVerts.Num());
		for (i = 0; i < MeshVerts.Num(); i++)
		{
			int iPoint = MeshVerts[i];
			const CSkelMeshVertex &V = Lod.Verts[iPoint];
			Ar.Printf("\tvert %d ( %f %f ) %d %d\n",
				i, V.UV[0].U, V.UV[0].V, MeshWeights[iPoint], Weights[iPoint].Inf.Num());
		}
		// triangles
		Ar.Printf("\n\tnumtris %d\n", Sec.NumFaces);
		for (i = 0; i < Sec.NumFaces; i++)
		{
			Ar.Printf("\ttri %d", i);
#if MIRROR_MESH
			for (int j = 2; j >= 0; j--)
#else
			for (int j = 0; j < 3; j++)
#endif
				Ar.Printf(" %d", BackWedge[Index(Lod.Indices, Sec.FirstIndex + i * 3 + j)]);
			Ar.Printf("\n");
		}
		// weights
		Ar.Printf("\n\tnumweights %d\n", WeightIndex);
		int saveWeightIndex = WeightIndex;
		WeightIndex = 0;
		for (i = 0; i < Lod.NumVerts; i++)
		{
			if (!UsedVerts[i]) continue;
			for (int j = 0; j < Weights[i].Inf.Num(); j++)
			{
				const VertInfluence &I = Weights[i].Inf[j];
				CVec3 v;
				v = Lod.Verts[i].Position;
#if MIRROR_MESH
				v[1] *= -1;						// y
#endif
				BoneCoords[I.Bone].TransformPoint(v, v);
				Ar.Printf(
					"\tweight %d %d %f ( %f %f %f )\n",
					WeightIndex, I.Bone, I.Weight, VECTOR_ARG(v)
				);
				WeightIndex++;
			}
		}
		assert(saveWeightIndex == WeightIndex);

		// mesh footer
		Ar.Printf("}\n");
	}

	unguard;
}


void ExportMd5Anim(const CAnimSet *Anim, FArchive &Ar)
{
	guard(ExportMd5Anim);

	int numBones = Anim->TrackBoneNames.Num();
	UObject *OriginalAnim = Anim->OriginalAnim;

	char basename[512];
	appSprintf(ARRAY_ARG(basename), "%s/%s", GetExportPath(OriginalAnim), OriginalAnim->Name);
	appMakeDirectory(basename);

	for (int AnimIndex = 0; AnimIndex < Anim->Sequences.Num(); AnimIndex++)
	{
		int i;
		const CAnimSequence &S = Anim->Sequences[AnimIndex];

		char FileName[512];
		appSprintf(ARRAY_ARG(FileName), "%s/%s.md5anim", basename, *S.Name);

		FFileReader Ar(FileName, false);

		Ar.Printf(
			"MD5Version 10\n"
			"commandline \"Created with UE Viewer\"\n"
			"\n"
			"numFrames %d\n"
			"numJoints %d\n"
			"frameRate %g\n"
			"numAnimatedComponents %d\n"
			"\n",
			S.NumFrames,
			numBones,
			S.Rate,
			numBones * 6
		);

		// skeleton
		Ar.Printf("hierarchy {\n");
		for (i = 0; i < numBones; i++)
		{
			Ar.Printf("\t\"%s\" %d %d %d\n", *Anim->TrackBoneNames[i], (i == 0) ? -1 : 0, 63, i * 6);
				// ParentIndex is unknown for UAnimSet, so always write "0"
				// here: 6 is number of components per frame, 63 = (1<<6)-1 -- flags "all components are used"
		}

		// bounds
		Ar.Printf("}\n\nbounds {\n");
		for (i = 0; i < S.NumFrames; i++)
			Ar.Printf("\t( -100 -100 -100 ) ( 100 100 100 )\n");	//!! dummy
		Ar.Printf("}\n\n");

		// baseframe and frames
		for (int Frame = -1; Frame < S.NumFrames; Frame++)
		{
			int t = Frame;
			if (Frame == -1)
			{
				Ar.Printf("baseframe {\n");
				t = 0;
			}
			else
				Ar.Printf("frame %d {\n", Frame);

			for (int b = 0; b < numBones; b++)
			{
				CVec3 BP;
				CQuat BO;
				S.Tracks[b].GetBonePosition(t, S.NumFrames, false, BP, BO);
				if (!b) BO.Conjugate();			// root bone
#if MIRROR_MESH
				BO.y  *= -1;
				BO.w  *= -1;
				BP[1] *= -1;					// y
#endif
				if (BO.w < 0) BO.Negate();		// W-component of quaternion will be removed ...
				if (Frame < 0)
					Ar.Printf("\t( %f %f %f ) ( %.10f %.10f %.10f )\n", VECTOR_ARG(BP), BO.x, BO.y, BO.z);
				else
					Ar.Printf("\t%f %f %f %.10f %.10f %.10f\n", VECTOR_ARG(BP), BO.x, BO.y, BO.z);
			}
			Ar.Printf("}\n\n");
		}
	}

	unguard;
}
