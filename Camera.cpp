//***************************************************************************************
// Camera.h by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Camera.h"

using namespace DirectX;

Camera::Camera()
{
	SetLens(0.25f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
}

Camera::~Camera()
{
}

XMVECTOR Camera::GetPosition()const
{
	return XMLoadFloat3(&mPosition);
}

XMFLOAT3 Camera::GetPosition3f()const
{
	return mPosition;
}

void Camera::SetPosition(float x, float y, float z)
{
	mPosition = XMFLOAT3(x, y, z);
	mViewDirty = true;
}

void Camera::SetPosition(const XMFLOAT3& v)
{
	mPosition = v;
	mViewDirty = true;
}

XMVECTOR Camera::GetRight()const
{
	return XMLoadFloat3(&mRight);
}

XMFLOAT3 Camera::GetRight3f()const
{
	return mRight;
}

XMVECTOR Camera::GetUp()const
{
	return XMLoadFloat3(&mUp);
}

XMFLOAT3 Camera::GetUp3f()const
{
	return mUp;
}

XMVECTOR Camera::GetLook()const
{
	return XMLoadFloat3(&mLook);
}

XMFLOAT3 Camera::GetLook3f()const
{
	return mLook;
}

float Camera::GetNearZ()const
{
	return mNearZ;
}

float Camera::GetFarZ()const
{
	return mFarZ;
}

float Camera::GetAspect()const
{
	return mAspect;
}

float Camera::GetFovY()const
{
	return mFovY;
}

float Camera::GetFovX()const
{
	// tan(fov/2) = (w/2) / d
	// fov = 2 * atan((w/2) / d)
	float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / mNearZ);
}

float Camera::GetNearWindowWidth()const
{
	// aspetc ratio r di area client è la stessa in near e far (per salvaguardare proporzioni).
	// r = w / h
	// w = r * h
	return mAspect * mNearWindowHeight;
}

float Camera::GetNearWindowHeight()const
{
	return mNearWindowHeight;
}

float Camera::GetFarWindowWidth()const
{
	return mAspect * mFarWindowHeight;
}

float Camera::GetFarWindowHeight()const
{
	return mFarWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	// Imposta le proprietà del frustum
	mFovY = fovY;
	mAspect = aspect;
	mNearZ = zn;
	mFarZ = zf;

	// tan(fov/2) = (h/2)/d 
	// (con h altezza e d distanza di piani near e far)
	// (fov angolo tra lati contrapposti del frustum)
	// Quindi:
	// h = 2 * d * tan(fov/2)
	mNearWindowHeight = 2.0f * mNearZ * tanf(0.5f * mFovY);
	mFarWindowHeight = 2.0f * mFarZ * tanf(0.5f * mFovY);

	// Calcola matrice di proiezione e la salva.
	XMMATRIX P = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
	XMStoreFloat4x4(&mProj, P);
}

void Camera::LookAt(FXMVECTOR pos, FXMVECTOR target, FXMVECTOR worldUp)
{
	XMVECTOR L = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR R = XMVector3Normalize(XMVector3Cross(worldUp, L));
	XMVECTOR U = XMVector3Cross(L, R);

	XMStoreFloat3(&mPosition, pos);
	XMStoreFloat3(&mLook, L);        // mLook  = L = target - pos
	XMStoreFloat3(&mRight, R);       // mRight = R = worldUp  x L
	XMStoreFloat3(&mUp, U);          // mUp    = U = L x R

	mViewDirty = true;
}

void Camera::LookAt(const XMFLOAT3& pos, const XMFLOAT3& target, const XMFLOAT3& up)
{
	XMVECTOR P = XMLoadFloat3(&pos);
	XMVECTOR T = XMLoadFloat3(&target);
	XMVECTOR U = XMLoadFloat3(&up);

	LookAt(P, T, U);

	mViewDirty = true;
}

XMMATRIX Camera::GetView()const
{
	assert(!mViewDirty);
	return XMLoadFloat4x4(&mView);
}

XMMATRIX Camera::GetProj()const
{
	return XMLoadFloat4x4(&mProj);
}


XMFLOAT4X4 Camera::GetView4x4f()const
{
	assert(!mViewDirty);
	return mView;
}

XMFLOAT4X4 Camera::GetProj4x4f()const
{
	return mProj;
}

void Camera::Strafe(float d)
{
	// mPosition += d*mRight
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR r = XMLoadFloat3(&mRight);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, r, p));  // s * r + p

	mViewDirty = true;
}

void Camera::Walk(float d)
{
	// mPosition += d*mLook
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = XMLoadFloat3(&mLook);
	XMVECTOR p = XMLoadFloat3(&mPosition);
	XMStoreFloat3(&mPosition, XMVectorMultiplyAdd(s, l, p));  // s * l + p

	mViewDirty = true;
}

void Camera::Pitch(float angle)
{
	// Ruota up e look rispetto a right.

	XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&mRight), angle);

	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

	mViewDirty = true;
}

void Camera::RotateY(float angle)
{
	// Ruota up, look e right rispetto all'asse delle Y dello spazio world.

	XMMATRIX R = XMMatrixRotationY(angle);

	XMStoreFloat3(&mRight, XMVector3TransformNormal(XMLoadFloat3(&mRight), R));
	XMStoreFloat3(&mUp, XMVector3TransformNormal(XMLoadFloat3(&mUp), R));
	XMStoreFloat3(&mLook, XMVector3TransformNormal(XMLoadFloat3(&mLook), R));

	mViewDirty = true;
}

void Camera::UpdateViewMatrix()
{
	if (mViewDirty)
	{
		XMVECTOR R = XMLoadFloat3(&mRight);
		XMVECTOR U = XMLoadFloat3(&mUp);
		XMVECTOR L = XMLoadFloat3(&mLook);
		XMVECTOR P = XMLoadFloat3(&mPosition);

		// Riortogonalizza e normalizza gli assi del sistema della camera.
		// Necessario perché i piccoli errori di precisione che si hanno
		// inevitabilmente durante i calcoli effettuati su tali assi possono 
		// comprometterne, alla lunga, lunghezza ed ortogonalità.
		L = XMVector3Normalize(L);
		U = XMVector3Normalize(XMVector3Cross(L, R));

		// U ed L già orto-normali quindi non c'è necessità di normalizzare
		// il prodotto vettoriale.
		R = XMVector3Cross(U, L);

		// Riempe gli elementi della matrice view.
		float x = -XMVectorGetX(XMVector3Dot(P, R));
		float y = -XMVectorGetX(XMVector3Dot(P, U));
		float z = -XMVectorGetX(XMVector3Dot(P, L));

		XMStoreFloat3(&mRight, R);
		XMStoreFloat3(&mUp, U);
		XMStoreFloat3(&mLook, L);

		mView(0, 0) = mRight.x;
		mView(1, 0) = mRight.y;
		mView(2, 0) = mRight.z;
		mView(3, 0) = x;

		mView(0, 1) = mUp.x;
		mView(1, 1) = mUp.y;
		mView(2, 1) = mUp.z;
		mView(3, 1) = y;

		mView(0, 2) = mLook.x;
		mView(1, 2) = mLook.y;
		mView(2, 2) = mLook.z;
		mView(3, 2) = z;

		mView(0, 3) = 0.0f;
		mView(1, 3) = 0.0f;
		mView(2, 3) = 0.0f;
		mView(3, 3) = 1.0f;

		mViewDirty = false;
	}
}

void ThirdPersonCamera::LookAt(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up)
{
	XMVECTOR L = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&pos)));
	XMVECTOR R = XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&up), L));
	XMVECTOR U = XMVector3Cross(L, R);

	XMStoreFloat3(&mLook, L);
	XMStoreFloat3(&mRight, R);
	XMStoreFloat3(&mUp, U);

	mPosition = pos;
	mTarget = target;
	//XMVECTOR length = XMVector3Length(XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&pos)));
	//mRadius = length.m128_f32[0];
	XMFLOAT3 length;
	XMStoreFloat3(&length, XMVector3Length(XMVectorSubtract(XMLoadFloat3(&target), XMLoadFloat3(&pos))));
	mRadius = length.x;

	mViewDirty = true;
}

void ThirdPersonCamera::Pitch(float angle)
{
	//  0° < mPhi < 90°
	mPhi += angle;
	mPhi = MathHelper::Clamp(mPhi, 0.05f, XM_PI / 2.0f - 0.01f);

	mViewDirty = true;
}

void ThirdPersonCamera::RotateY(float angle)
{
	//  mTheta sempre in [0°, 360°)
	// mTheta = (mTheta + angle) % (MathF.PI * 2.0f);
	mTheta = XMScalarModAngle(mTheta + angle);

	mViewDirty = true;
}

void ThirdPersonCamera::Walk(float d)
{
	// mTarget += d*mLook
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = { mLook.x, 0, mLook.z }; // proiez. ortog. di look su piano XZ
	XMVECTOR t = XMLoadFloat3(&mTarget);
	XMStoreFloat3(&mTarget, XMVectorMultiplyAdd(s, l, t)); // s * l + t

	mViewDirty = true;
}

void ThirdPersonCamera::Strafe(float d)
{
	// mTarget += d*mRight
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR r = { mRight.x, 0, mRight.z };  // proiez. ortog. di right su piano XZ
	XMVECTOR t = XMLoadFloat3(&mTarget);
	XMStoreFloat3(&mTarget, XMVectorMultiplyAdd(s, r, t)); // s * r + t

	mViewDirty = true;
}

void ThirdPersonCamera::SetTarget3f(XMFLOAT3 targetPos)
{
	mTarget = targetPos;

	mViewDirty = true;
}

XMFLOAT3 ThirdPersonCamera::GetTarget3f()
{
	return mTarget;
}

XMVECTOR ThirdPersonCamera::GetTarget() const
{
	return XMLoadFloat3(&mTarget);
}

void ThirdPersonCamera::AddToRadius(float d)
{
	mRadius += d;
	mRadius = MathHelper::Clamp(mRadius, 5.0f, 25.0f);

	mViewDirty = true;
}

float ThirdPersonCamera::GetRadius()
{
	return mRadius;
}

void ThirdPersonCamera::UpdateViewMatrix()
{
	if (mViewDirty)
	{
		// Converte da coordinate sferiche a coordinate cartesiane.
		// Assi invertiti
		//float x = mRadius * sinf(mPhi) * cosf(mTheta);
		//float z = mRadius * sinf(mPhi) * sinf(mTheta);
		//float y = mRadius * cosf(mPhi);

		// Converte da coordinate sferiche a coordinate cartesiane.
		float x = mRadius * cosf(mPhi) * sinf(mTheta);
		float z = mRadius * cosf(mPhi) * cosf(mTheta);
		float y = mRadius * sinf(mPhi);

		mPosition = { mTarget.x + x, mTarget.y + y, mTarget.z + z };

		XMStoreFloat4x4(&mView, XMMatrixLookAtLH(XMLoadFloat3(&mPosition), XMLoadFloat3(&mTarget), g_XMIdentityR1));

		XMStoreFloat3(&mRight, XMVector3Normalize({ mView._11, mView._21, mView._31 }));
		XMStoreFloat3(&mLook, XMVector3Normalize({ mView._13, mView._23, mView._33 }));
		XMStoreFloat3(&mUp, XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&mLook), XMLoadFloat3(&mRight))));

		mViewDirty = false;
	}
}
