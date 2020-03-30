/*!
 * \file CtaStraBaseCtx.h
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 * 
 * \brief 
 */
#pragma once
#include <unordered_map>
#include "ICtaStraCtx.h"

#include "../Share/WTSDataDef.hpp"
#include "../Share/BoostFile.hpp"

class CtaStrategy;

NS_OTP_BEGIN

class WtCtaEngine;

const char COND_ACTION_OL = 0;	//����
const char COND_ACTION_CL = 1;	//ƽ��
const char COND_ACTION_OS = 2;	//����
const char COND_ACTION_CS = 3;	//ƽ��
const char COND_ACTION_SP = 4;	//ֱ�����ò�λ

typedef struct _CondEntrust
{
	WTSCompareField _field;
	WTSCompareType	_alg;
	double			_target;

	int32_t			_qty;

	char			_action;	//0-����, 1-ƽ��, 2-����, 3-ƽ��

	char			_code[MAX_INSTRUMENT_LENGTH];
	char			_usertag[32];


	_CondEntrust()
	{
		memset(this, 0, sizeof(_CondEntrust));
	}

} CondEntrust;

typedef std::vector<CondEntrust>	CondList;
typedef std::unordered_map<std::string, CondList>	CondEntrustMap;


class CtaStraBaseCtx : public ICtaStraCtx
{
public:
	CtaStraBaseCtx(WtCtaEngine* engine, const char* name);
	virtual ~CtaStraBaseCtx();

private:
	void	init_outputs();
	inline void log_signal(const char* stdCode, int32_t target, double price, uint64_t gentime, const char* usertag = "");
	inline void	log_trade(const char* stdCode, bool isLong, bool isOpen, uint64_t curTime, double price, int32_t qty, const char* userTag = "", double fee = 0.0);
	inline void	log_close(const char* stdCode, bool isLong, uint64_t openTime, double openpx, uint64_t closeTime, double closepx, int32_t qty,
		double profit, double totalprofit = 0, const char* enterTag = "", const char* exitTag = "");

	void	save_data(uint32_t flag = 0xFFFFFFFF);
	void	load_data(uint32_t flag = 0xFFFFFFFF);

	void	load_userdata();
	void	save_userdata();

	void	update_dyn_profit(const char* stdCode, double price);

	void	do_set_position(const char* stdCode, int32_t qty, const char* userTag = "", bool bTriggered = false);
	void	append_signal(const char* stdCode, int32_t qty, const char* userTag = "");

	inline CondList& get_cond_entrusts(const char* stdCode);
	

public:
	virtual uint32_t id() { return _context_id; }

	//�ص�����
	virtual void on_init() override;
	virtual void on_session_begin() override;
	virtual void on_session_end() override;
	virtual void on_tick(const char* stdCode, WTSTickData* newTick, bool bEmitStrategy = true) override;
	virtual void on_bar(const char* stdCode, const char* period, uint32_t times, WTSBarStruct* newBar) override;
	virtual bool on_schedule(uint32_t curDate, uint32_t curTime) override;

	virtual void enum_position(FuncEnumPositionCallBack cb) override;


	//////////////////////////////////////////////////////////////////////////
	//���Խӿ�
	virtual void stra_enter_long(const char* stdCode, int32_t qty, const char* userTag = "", double limitprice = 0.0, double stopprice = 0.0) override;
	virtual void stra_enter_short(const char* stdCode, int32_t qty, const char* userTag = "", double limitprice = 0.0, double stopprice = 0.0) override;
	virtual void stra_exit_long(const char* stdCode, int32_t qty, const char* userTag = "", double limitprice = 0.0, double stopprice = 0.0) override;
	virtual void stra_exit_short(const char* stdCode, int32_t qty, const char* userTag = "", double limitprice = 0.0, double stopprice = 0.0) override;

	virtual int32_t stra_get_position(const char* stdCode, const char* userTag = "") override;
	virtual void stra_set_position(const char* stdCode, int32_t qty, const char* userTag = "", double limitprice = 0.0, double stopprice = 0.0) override;
	virtual double stra_get_price(const char* stdCode) override;

	virtual uint32_t stra_get_date() override;
	virtual uint32_t stra_get_time() override;

	virtual uint64_t stra_get_first_entertime(const char* stdCode) override;
	virtual uint64_t stra_get_last_entertime(const char* stdCode) override;
	virtual double stra_get_last_enterprice(const char* stdCode) override;
	virtual double stra_get_position_avgpx(const char* stdCode) override;
	virtual double stra_get_position_profit(const char* stdCode) override;

	virtual uint64_t stra_get_detail_entertime(const char* stdCode, const char* userTag) override;
	virtual double stra_get_detail_cost(const char* stdCode, const char* userTag) override;
	virtual double stra_get_detail_profit(const char* stdCode, const char* userTag, int flag = 0) override;

	virtual WTSCommodityInfo* stra_get_comminfo(const char* stdCode) override;
	virtual WTSKlineSlice*	stra_get_bars(const char* stdCode, const char* period, uint32_t count, bool isMain = false) override;
	virtual WTSTickSlice*	stra_get_ticks(const char* stdCode, uint32_t count) override;
	virtual WTSTickData*	stra_get_last_tick(const char* stdCode) override;

	virtual void sub_ticks(const char* stdCode) override;

	virtual void stra_log_text(const char* fmt, ...) override;

	virtual void stra_save_user_data(const char* key, const char* val) override;

	virtual const char* stra_load_user_data(const char* key, const char* defVal = "") override;

protected:
	uint32_t		_context_id;
	WtCtaEngine*	_engine;

	uint64_t		_total_calc_time;	//�ܼ���ʱ��
	uint32_t		_emit_times;		//�ܼ������

	std::string		_main_key;

	typedef struct _KlineTag
	{
		bool			_closed;

		_KlineTag() :_closed(false){}

	} KlineTag;
	typedef std::unordered_map<std::string, KlineTag> KlineTags;
	KlineTags	_kline_tags;

	typedef std::unordered_map<std::string, double> PriceMap;
	PriceMap		_price_map;

	typedef struct _DetailInfo
	{
		bool		_long;
		double		_price;
		int32_t		_volumn;
		uint64_t	_opentime;
		uint32_t	_opentdate;
		double		_max_profit;
		double		_max_loss;
		double		_profit;
		char		_opentag[32];

		_DetailInfo()
		{
			memset(this, 0, sizeof(_DetailInfo));
		}
	} DetailInfo;

	typedef struct _PosInfo
	{
		int32_t		_volumn;
		double		_closeprofit;
		double		_dynprofit;

		std::vector<DetailInfo> _details;

		_PosInfo()
		{
			_volumn = 0;
			_closeprofit = 0;
			_dynprofit = 0;
		}
	} PosInfo;
	typedef std::unordered_map<std::string, PosInfo> PositionMap;
	PositionMap		_pos_map;

	typedef struct _SigInfo
	{
		int32_t		_volumn;
		std::string	_usertag;
		double		_sigprice;
		bool		_triggered;
		uint64_t	_gentime;

		_SigInfo()
		{
			_volumn = 0;
			_sigprice = 0;
			_triggered = false;
			_gentime = 0;
		}
	}SigInfo;
	typedef std::unordered_map<std::string, SigInfo>	SignalMap;
	SignalMap		_sig_map;

	BoostFilePtr	_trade_logs;
	BoostFilePtr	_close_logs;
	BoostFilePtr	_fund_logs;
	BoostFilePtr	_sig_logs;

	CondEntrustMap	_condtions;
	uint64_t		_last_cond_min;	//�ϴ�������������ʱ��

	//�Ƿ��ڵ����еı��
	bool			_is_in_schedule;	//�Ƿ����Զ�������

	//�û�����
	typedef std::unordered_map<std::string, std::string> StringHashMap;
	StringHashMap	_user_datas;
	bool			_ud_modified;

	typedef struct _StraFundInfo
	{
		double	_total_profit;
		double	_total_dynprofit;
		double	_total_fees;

		_StraFundInfo()
		{
			memset(this, 0, sizeof(_StraFundInfo));
		}
	} StraFundInfo;

	StraFundInfo		_fund_info;
};


NS_OTP_END