// dxc -Fo ComputeShaderSampFeedback.dxil -T cs_6_6 -E main -HV 2021 ComputeshaderSampFeedback.hlsl
#define RS  "DescriptorTable(UAV(u0), UAV(u1)), " \
			"DescriptorTable(SRV(t0), SRV(t1)), " \
			"DescriptorTable(Sampler(s0))"

RWTexture2D<float4> OutputTexture : register(u0);
Texture2D<float4> InputTexture : register(t0);
SamplerState Sampler : register(s0);
Texture2D<uint> ResidentMinMip : register(t1);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> FeedbackTexture : register(u1);

[numthreads(8, 8, 1)]
[RootSignature(RS)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 offset = int2(0, 0);
	float2 uv = float2(DTid.x / 3840.0, DTid.y / 2160.0);
	int3 minMipUV = int3(uv.x * 30, uv.y * 16, 0);

	uint minMip = ResidentMinMip.Load(minMipUV);
	FeedbackTexture.WriteSamplerFeedbackLevel(InputTexture, Sampler, uv, minMip);
	OutputTexture[DTid.xy] = InputTexture.Sample(Sampler, uv, offset, minMip);
}
