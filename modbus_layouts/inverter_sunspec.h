#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <cmath>

using uint16 = uint16_t;
using int16 = int16_t;
using uint32 = uint32_t;
using float32 = float;
template <int N>
using string = std::array<char, N>;
using enum16 = uint16_t;
using bitfield16 = uint16_t;
using bitfield32 = uint32_t;
using acc32 = uint32_t;
using acc64 = uint64_t;
using sunssf = int16_t; // scale factor means: 10^{sunssf}, so -1 = .1, 2 = 100, -2 = .01
using pad = uint16_t;
constexpr inline uint16_t modbus_swap(uint16_t v) { return (v >> 8) | ((v & 0xff) << 8) ; }
constexpr inline int16_t modbus_swap_i16(int16_t v) { return int16_t((uint16_t(v) >> 8) | ((uint16_t(v) & 0xff) << 8)) ; }
constexpr inline float modbus_swap_f(float v) {float r; uint8_t *o = (uint8_t*)&r, *i = (uint8_t*)&v; o[0] = i[3]; o[1] = i[2]; o[2] = i[1]; o[3] = i[0]; return r;}
constexpr inline float to_float(int val, sunssf scale) { return val * std::pow<float>(10, scale); }
constexpr inline uint16_t from_float(float val, sunssf scale) { return val * std::pow<float>(10, -scale); }
template<typename T>
constexpr inline uint16 model_size() { return sizeof(T) / 2 - 2; }
template<typename T>
constexpr inline uint16 model_sunspec_size() { return  modbus_swap(uint16_t(sizeof(T) / 2 - 2)); } // division by 2 as we calc in halfes, -2 for id and length

constexpr uint16 MODE_CHARGE{0};
constexpr uint16 MODE_DISCHARGE{1};

template<unsigned int N>
constexpr std::string_view to_sv(const string<N> &s) { return std::string_view{s.data(), s.data() + N}; }

// the following is an exact copy of registers for a fronius inverter

#pragma pack(push, 1)
struct model_start {
	string<4> 	sid{'S','u','n','S'};
};
struct model_common {
	static constexpr uint16_t ID{modbus_swap(1)};
	uint16		id_common{ID};
	uint16		length_common{model_sunspec_size<model_common>()};
	string<32> 	manufacturer{"Lachei"};
	string<32> 	device_model{"victron iot 1.0"};
	string<16> 	options{""};
	string<16> 	sw_meter_version{"1.0"};
	string<32> 	serial_number{"67"};
	uint16		device_address{modbus_swap(1)};
};
struct model_inverter {
	static constexpr uint16_t ID{modbus_swap(113)};
	uint16		id_inverter{ID};
	uint16		length_inverter{model_sunspec_size<model_inverter>()};
	float32   	A       {};
	float32   	AphA    {};
	float32   	AphB    {};
	float32   	AphC    {};
	float32   	PPVphAB {};
	float32   	PPVphBC {};
	float32   	PPVphCA {};
	float32   	PhVphA  {};
	float32   	PhVphB  {};
	float32   	PhVphC  {};
	float32   	W       {};
	float32   	Hz      {};
	float32   	VA      {};
	float32   	VAr     {};
	float32   	PF      {};
	float32   	WH      {};
	float32   	DCA     {};
	float32   	DCV     {};
	float32   	DCW     {};
	float32   	TmpCab  {};
	float32   	TmpSnk  {};
	float32   	TmpTrns {};
	float32   	TmpOt   {};
	enum16    	St      {};
	enum16    	StVnd   {};
	bitfield32	Evt1    {};
	bitfield32	Evt2    {};
	bitfield32	EvtVnd1 {};
	bitfield32	EvtVnd2 {};
	bitfield32	EvtVnd3 {};
	bitfield32	EvtVnd4 {};
};
struct model_nameplate {
	static constexpr uint16_t ID{modbus_swap(120)};
	uint16		id_nameplate	{ID};
	uint16		length_nameplate{model_sunspec_size<model_nameplate>()};
	enum16		DERTyp		{modbus_swap(82)}; // PV_STOR
	uint16		WRtg		{};
	sunssf		WRtg_SF		{modbus_swap_i16(1)};
	uint16		VARtg		{};
	sunssf		VARtg_SF	{modbus_swap_i16(1)};
	int16 		VArRtgQ1 	{};
	int16 		VArRtgQ2 	{};
	int16 		VArRtgQ3 	{};
	int16 		VArRtgQ4 	{};
	sunssf		VArRtg_SF	{modbus_swap_i16(1)};
	uint16		ARtg		{};
	sunssf		ARtg_SF		{modbus_swap_i16(2)};
	int16 		PFRtgQ1		{};
	int16 		PFRtgQ2		{};
	int16 		PFRtgQ3		{};
	int16 		PFRtgQ4		{};
	sunssf		PFRtg_SF	{modbus_swap_i16(-3)};
	uint16		WHRtg		{};
	sunssf		WHRtg_SF	{modbus_swap(1)};
	uint16		AhrRtg		{};
	sunssf		AhrRtg_SF	{modbus_swap(0)};
	uint16		MaxChaRte	{};
	sunssf		MaxChaRte_SF	{modbus_swap(0)};
	uint16		MaxDisChaRte	{};
	sunssf		MaxDisChaRte_SF	{modbus_swap(0)};
	pad   		pad_nampelate	{};
};
struct model_settings {
	static constexpr uint16_t ID{modbus_swap(121)};
	uint16		id_settings	{ID};
	uint16    	length_settings	{model_sunspec_size<model_settings>()};
	uint16    	WMax		{};
	uint16    	VRef		{};
	int16     	VRefOfs		{modbus_swap_i16(0)};
	uint16    	VMax		{};
	uint16    	VMin		{};
	uint16    	VAMax		{};
	int16     	VArMaxQ1	{};
	int16     	VArMaxQ2	{};
	int16     	VArMaxQ3	{};
	int16     	VArMaxQ4	{};
	uint16    	WGra		{};
	int16     	PFMinQ1		{};
	int16     	PFMinQ2		{};
	int16     	PFMinQ3		{};
	int16     	PFMinQ4		{};
	enum16    	VArAct		{};
	enum16    	ClcTotVA	{};
	uint16    	MaxRmpRte	{};
	uint16    	ECPNomHz	{};
	enum16    	ConnPh		{};
	sunssf    	WMax_SF		{modbus_swap_i16(1)};
	sunssf    	VRef_SF         {modbus_swap_i16(0)};
	sunssf    	VRefOfs_SF      {modbus_swap_i16(0)};
	sunssf    	VMinMax_SF      {modbus_swap_i16(0)};
	sunssf    	VAMax_SF        {modbus_swap_i16(1)};
	sunssf    	VArMax_SF       {modbus_swap_i16(1)};
	sunssf    	WGra_SF         {modbus_swap_i16(0)};
	sunssf    	PFMin_SF        {modbus_swap_i16(-3)};
	sunssf    	MaxRmpRte_SF    {modbus_swap_i16(0)};
	sunssf    	ECPNomHz_SF     {modbus_swap_i16(0)};
};
struct model_status {
	static constexpr uint16_t ID{modbus_swap(122)};
	uint16		id_status	{ID};
	uint16    	length_status	{model_sunspec_size<model_status>()};
	bitfield16	PVConn		{modbus_swap(1 | 2 | 4)}; // Bit0: Connected, Bit1: Available, Bit2: Operating
	bitfield16	StorConn	{modbus_swap(1 | 2 | 4)}; // Bit0: Connected, Bit1: Available, Bit2: Operating
	bitfield16	ECPConn		{modbus_swap(1)}; // Bit0: connected
	acc64     	ActWh		{};
	acc64     	ActVAh		{};
	acc64     	ActVArhQ1	{};
	acc64     	ActVArhQ2	{};
	acc64     	ActVArhQ3	{};
	acc64     	ActVArhQ4	{};
	int16     	VArAval		{};
	sunssf    	VArAval_SF	{};
	uint16    	WAval		{};
	sunssf    	WAval_SF	{};
	bitfield32	StSetLimMsk	{};
	bitfield32	StActCtl	{};
	string<8>	TmSrc		{"RTC"};
	uint32    	Tms		{};
	bitfield16	RtSt		{};
	uint16    	Ris		{};
	sunssf    	Ris_SF		{};
};
struct model_controls {
	static constexpr uint16_t ID{modbus_swap(123)};
	uint16		id_controls		{ID};
	uint16  	id_length		{model_sunspec_size<model_controls>()};
	uint16  	Conn_WinTms		{};
	uint16  	Conn_RvrtTms		{};
	enum16  	Conn			{modbus_swap(1)}; // 1 connected
	uint16  	WMaxLimPct		{modbus_swap(10000)}; // 100 percent with WMaxLimPct_SF
	uint16  	WMaxLimPct_WinTms	{};
	uint16  	WMaxLimPct_RvrtTms	{};
	uint16  	WMaxLimPct_RmpTms	{};
	enum16  	WMaxLim_Ena      	{modbus_swap(1)};
	int16   	OutPFSet		{modbus_swap_i16(1000)}; // 1 with OutPFSet_SF
	uint16  	OutPFSet_WinTms		{};
	uint16  	OutPFSet_RvrtTms	{};
	uint16  	OutPFSet_RmpTms		{};
	enum16  	OutPFSet_Ena		{};
	int16   	VArWMaxPct		{};
	int16   	VArMaxPct		{};
	int16   	VArAvalPct		{};
	uint16  	VArPct_WinTms		{};
	uint16  	VArPct_RvrtTms		{};
	uint16  	VArPct_RmpTms		{};
	enum16  	VArPct_Mod		{};
	enum16  	VArPct_Ena		{};
	sunssf  	WMaxLimPct_SF		{modbus_swap_i16(-2)};
	sunssf  	OutPFSet_SF		{modbus_swap_i16(-3)};
	sunssf  	VArPct_SF		{};
};
struct mppt_infos{
	uint16    	module_ID	{};
	string<16>	module_IDStr	{"MPPT1"};
	uint16    	module_DCA	{};
	uint16    	module_DCV	{};
	uint16    	module_DCW	{};
	acc32     	module_DCWH	{};
	uint32    	module_Tms	{};
	int16     	module_Tmp	{};
	enum16    	module_DCSt	{};
	bitfield32	module_DCEvt	{};
};
struct model_mppt {
	static constexpr uint16_t ID{modbus_swap(160)};
	static constexpr uint16_t MPPT_COUNT{4};
	uint16		id_mqtt		{ID};
	uint16    	length_mqtt	{model_sunspec_size<model_mppt>()};
	sunssf    	DCA_SF		{};
	sunssf    	DCV_SF		{};
	sunssf    	DCW_SF		{};
	sunssf    	DCWH_SF		{};
	bitfield32	Evt		{};
	uint16    	N		{modbus_swap(4)};
	uint16    	TmsPer		{};
	uint16    	module_1_ID	{};
	string<16>	module_1_IDStr	{"MPPT1"};
	uint16    	module_1_DCA	{};
	uint16    	module_1_DCV	{};
	uint16    	module_1_DCW	{};
	acc32     	module_1_DCWH	{};
	uint32    	module_1_Tms	{};
	int16     	module_1_Tmp	{};
	enum16    	module_1_DCSt	{};
	bitfield32	module_1_DCEvt	{};
	uint16    	module_2_ID	{};
	string<16>	module_2_IDStr	{"MPPT2"};
	uint16    	module_2_DCA	{};
	uint16    	module_2_DCV	{};
	uint16    	module_2_DCW	{};
	acc32     	module_2_DCWH	{};
	uint32    	module_2_Tms	{};
	int16     	module_2_Tmp	{};
	enum16    	module_2_DCSt	{};
	bitfield32	module_2_DCEvt	{};
	uint16    	module_3_ID	{};
	string<16>	module_3_IDStr	{"StCha3"};
	uint16    	module_3_DCA	{};
	uint16    	module_3_DCV	{};
	uint16    	module_3_DCW	{};
	acc32     	module_3_DCWH	{};
	uint32    	module_3_Tms	{};
	int16     	module_3_Tmp	{};
	enum16    	module_3_DCSt	{};
	bitfield32	module_3_DCEvt	{};
	uint16    	module_4_ID	{};
	string<16>	module_4_IDStr	{"StDisCha4"};
	uint16    	module_4_DCA	{};
	uint16    	module_4_DCV	{};
	uint16    	module_4_DCW	{};
	acc32     	module_4_DCWH	{};
	uint32    	module_4_Tms	{};
	int16     	module_4_Tmp	{};
	enum16    	module_4_DCSt	{};
	bitfield32	module_4_DCEvt	{};
};
struct model_storage {
	static constexpr uint16_t ID{modbus_swap(124)};
	uint16		id_storage		{ID};
	uint16		length_storage		{model_sunspec_size<model_storage>()};
	uint16    	WChaMax			{modbus_swap(10000)}; // use this in combination of StorCtl_Mod to set charge/discharge
	uint16    	WChaGra			{modbus_swap(100)};
	uint16    	WDisChaGra		{modbus_swap(100)};
	bitfield16	StorCtl_Mod		{}; // Bit0: Charge power override, Bit1: Discharge power override
	uint16    	VAChaMax		{};
	uint16    	MinRsvPct		{};
	uint16    	ChaState		{}; // SOC 
	uint16    	StorAval		{}; // not supported
	uint16    	InBatV			{}; // battery voltage
	enum16    	ChaSt			{}; // status state
	int16     	OutWRte			{modbus_swap_i16(10000)};
	int16     	InWRte			{modbus_swap_i16(10000)};
	uint16    	InOutWRte_WinTms	{};
	uint16    	InOutWRte_RvrtTms	{};
	uint16    	InOutWRte_RmpTms 	{};
	enum16    	ChaGriSet		{modbus_swap(1)}; // 1: Charging from grid enabled
	sunssf    	WChaMax_SF		{};
	sunssf    	WChaDisChaGra_SF	{};
	sunssf    	VAChaMax_SF		{};
	sunssf    	MinRsvPct_SF		{modbus_swap_i16(-2)};
	sunssf    	ChaState_SF		{modbus_swap_i16(-2)};
	sunssf    	StorAval_SF		{};
	sunssf    	InBatV_SF  		{};
	sunssf    	InOutWRte_SF		{modbus_swap_i16(-2)};
};
struct model_end {
	static constexpr uint16_t ID{0xffff};
	uint16		id_end		{ID};
	uint16		length_end	{0};
};

struct sunspec_registers: 
	public model_start,
	public model_common,
	public model_inverter,
	public model_nameplate,
	public model_settings,
	public model_status,
	public model_controls,
	public model_mppt,
	public model_storage,
	public model_end
{ constexpr static int OFFSET = 40000; };
#pragma pack(pop)

struct inverter_layout {
	// sunspec modbus layout only has writable halfs registers
	sunspec_registers halfs_registers{};
};
