#pragma once

#include "inverter_sunspec.h"

#pragma pack(push, 1)
struct model_meter {
	static constexpr uint16_t ID{modbus_swap(213)};
	uint16  	id_meter	{ID};
	uint16  	length_meter	{model_sunspec_size<model_meter>()};
	float32 	A		{};
	float32 	AphA		{};
	float32 	AphB		{};
	float32 	AphC		{};
	float32 	PhV		{};
	float32 	PhVphA		{};
	float32 	PhVphB		{};
	float32 	PhVphC		{};
	float32 	PPV		{};
	float32 	PPVphAB		{};
	float32 	PPVphBC		{};
	float32 	PPVphCA		{};
	float32 	Hz		{};
	float32 	W		{};
	float32 	WphA		{};
	float32 	WphB		{};
	float32 	WphC		{};
	float32 	VA		{};
	float32 	VAphA		{};
	float32 	VAphB		{};
	float32 	VAphC		{};
	float32 	VAR		{};
	float32 	VARphA		{};
	float32 	VARphB		{};
	float32 	VARphC		{};
	float32 	PF		{};
	float32 	PFphA		{};
	float32 	PFphB		{};
	float32 	PFphC		{};
	float32 	TotWhExp	{};
	float32 	TotWhExpPhA	{};
	float32 	TotWhExpPhB	{};
	float32 	TotWhExpPhC	{};
	float32 	TotWhImp	{};
	float32 	TotWhImpPhA	{};
	float32 	TotWhImpPhB	{};
	float32 	TotWhImpPhC	{};
	float32 	TotVAhExp	{};
	float32 	TotVAhExpPhA	{};
	float32 	TotVAhExpPhB	{};
	float32 	TotVAhExpPhC	{};
	float32 	TotVAhImp	{};
	float32 	TotVAhImpPhA	{};
	float32 	TotVAhImpPhB	{};
	float32 	TotVAhImpPhC	{};
	float32 	TotVArhImpQ1	{};
	float32 	TotVArhImpQ1phA	{};
	float32 	TotVArhImpQ1phB	{};
	float32 	TotVArhImpQ1phC	{};
	float32 	TotVArhImpQ2	{};
	float32 	TotVArhImpQ2phA	{};
	float32 	TotVArhImpQ2phB	{};
	float32 	TotVArhImpQ2phC	{};
	float32 	TotVArhExpQ3	{};
	float32 	TotVArhExpQ3phA	{};
	float32 	TotVArhExpQ3phB	{};
	float32 	TotVArhExpQ3phC	{};
	float32 	TotVArhExpQ4	{};
	float32 	TotVArhExpQ4phA	{};
	float32 	TotVArhExpQ4phB	{};
	float32 	TotVArhExpQ4phC	{};
	bitfield32      Evt		{};
};

struct meter_registers :
	public model_start,
	public model_common,
	public model_meter,
	public model_end
{ constexpr static int OFFSET = 40000; };
#pragma pack(pop)

struct meter_layout {
	meter_registers halfs_registers{};
};

