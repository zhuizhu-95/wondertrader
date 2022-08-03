/*!
 * \file TraderATP.cpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 *
 * \brief
 */
#include "TraderATP.h"

#include "../Includes/IBaseDataMgr.h"
#include "../Includes/WTSContractInfo.hpp"
#include "../Includes/WTSSessionInfo.hpp"
#include "../Includes/WTSTradeDef.hpp"
#include "../Includes/WTSError.hpp"
#include "../Includes/WTSVariant.hpp"

#include "../Share/ModuleHelper.hpp"

#include <boost/filesystem.hpp>
#include <iostream>

#ifdef _WIN32
#ifdef _WIN64
#pragma comment(lib, "../API/AtpTradeApi/x64/atptradeapi.lib")
#else
#pragma comment(lib, "../API/AtpTradeApi/x86/atptradeapi.lib")
#endif
#endif

 //By Wesley @ 2022.01.05
#include "../Share/fmtlib.h"
template<typename... Args>
inline void write_log(ITraderSpi* sink, WTSLogLevel ll, const char* format, const Args&... args)
{
	if (sink == NULL)
		return;

	const char* buffer = fmtutil::format(format, args...);

	sink->handleTraderLog(ll, buffer);
}

extern "C"
{
	EXPORT_FLAG ITraderApi* createTrader()
	{
		TraderATP *instance = new TraderATP();
		return instance;
	}

	EXPORT_FLAG void deleteTrader(ITraderApi* &trader)
	{
		if (NULL != trader)
		{
			delete trader;
			trader = NULL;
		}
	}
}

inline uint32_t makeRefID()
{
	static std::atomic<uint32_t> auto_refid(0);
	if (auto_refid == 0)
		auto_refid = (uint32_t)((TimeUtils::getLocalTimeNow() - TimeUtils::makeTime(20220101, 0)) / 1000 * 100);
	return auto_refid.fetch_add(1);
}


TraderATP::TraderATP()
	: _api(NULL)
	, _sink(NULL)
	, _ordref(makeRefID())
	, _reqid(1)
	, _orders(NULL)
	, _trades(NULL)
	, _positions(NULL)
	, _bd_mgr(NULL)
	, _tradingday(0)
	, _close_flag(false)
{

}


TraderATP::~TraderATP()
{

}


inline WTSDirectionType wrapATPSide(const ATPSideType side)
{
	if (side == ATPSideConst::kBuy || side == ATPSideConst::kFinancingBuy)
		return WDT_LONG;
	else if (side == ATPSideConst::kSell || side == ATPSideConst::kLoanSell)
		return WDT_SHORT;
	else
		return WDT_NET;
}

inline WTSOffsetType wrapOffsetType(ATPSideType dirType)
{
	if (dirType == ATPSideConst::kBuy || dirType == ATPSideConst::kFinancingBuy)
		return WOT_OPEN;
	else
		return WOT_CLOSE;
}

inline WTSOrderState wrapOrdStatus(ATPOrdStatusType orderState)
{
	switch (orderState)
	{
	case ATPOrdStatusConst::kFilled:
		return WOS_AllTraded;
	case ATPOrdStatusConst::kPartiallyFilled:
		return WOS_PartTraded_Queuing;
	case ATPOrdStatusConst::kPartiallyFilledPartiallyCancelled:
		return WOS_PartTraded_NotQueuing;
	case ATPOrdStatusConst::kWaitCancelled:
	case ATPOrdStatusConst::kPartiallyFilledWaitCancelled:
		return WOS_Cancelling;
	case ATPOrdStatusConst::kCancelled:
		return WOS_Canceled;
	case ATPOrdStatusConst::kSended:
	case ATPOrdStatusConst::kNew:
	case ATPOrdStatusConst::kProcessed:
		return WOS_Submitting;
	default:
		return WOS_Nottouched;
	}
}

/*
WTSEntrust* TraderATP::makeEntrust(XTPOrderInfo* order_info)
{
	std::string code, exchg;
	if (order_info->market == XTP_MKT_SH_A)
		exchg = "SSE";
	else
		exchg = "SZSE";
	code = order_info->ticker;
	WTSContractInfo* ct = _bd_mgr->getContract(code.c_str(), exchg.c_str());
	if (ct == NULL)
		return NULL;

	WTSEntrust* pRet = WTSEntrust::create(
		code.c_str(),
		(uint32_t)order_info->quantity,
		order_info->price,
		ct->getExchg());
	pRet->setContractInfo(ct);
	pRet->setDirection(wrapDirectionType(order_info->side, order_info->position_effect));
	pRet->setPriceType(wrapPriceType(order_info->price_type));
	pRet->setOffsetType(wrapOffsetType(order_info->position_effect));
	pRet->setOrderFlag(WOF_NOR);

	genEntrustID(pRet->getEntrustID(), order_info->order_client_id);

	const char* usertag = m_eidCache.get(pRet->getEntrustID());
	if (strlen(usertag) > 0)
		pRet->setUserTag(usertag);

	return pRet;
}
*/

WTSOrderInfo* TraderATP::makeOrderInfo(const APIOrderUnit* order_info)
{
	std::string code, exchg;
	if (order_info->market_id == 101)
		exchg = "SSE";
	else if (order_info->market_id == 102)
		exchg = "SZSE";

	code = order_info->security_id;
	WTSContractInfo* contract = _bd_mgr->getContract(code.c_str(), exchg.c_str());
	if (contract == NULL)
		return NULL;

	WTSOrderInfo* pRet = WTSOrderInfo::create();
	pRet->setContractInfo(contract);
	pRet->setPrice(order_info->order_price / 10000);
	pRet->setVolume((uint32_t)order_info->order_qty / 100);
	//pRet->setDirection(wrapATPSide(order_info->side));
	//pRet->setPriceType(wrapPriceType(order_info->price_type));
	pRet->setOrderFlag(WOF_NOR);
	pRet->setOffsetType(wrapOffsetType(order_info->side));

	pRet->setVolTraded((uint32_t)order_info->cum_qty / 100);
	pRet->setVolLeft((uint32_t)order_info->leaves_qty / 100);

	pRet->setCode(code.c_str());
	pRet->setExchange(contract->getExchg());

	pRet->setOrderDate((uint32_t)(order_info->transact_time / 1000000000));
	uint32_t uTime = order_info->transact_time % 1000000000;
	pRet->setOrderTime(TimeUtils::makeTime(pRet->getOrderDate(), uTime));

	pRet->setOrderState(wrapOrdStatus(order_info->ord_status));
	if (order_info->ord_status == ATPOrdStatusConst::kReject)
		pRet->setError(true);

	genEntrustID(pRet->getEntrustID(), order_info->client_seq_id);
	//pRet->setEntrustID(genEntrustID(pRet->getEntrustID(), order_info->client_seq_id).c_str());
	pRet->setOrderID(fmt::format("{}", order_info->order_id).c_str());

	pRet->setStateMsg("");

	const char* usertag = m_eidCache.get(pRet->getEntrustID());
	if (strlen(usertag) == 0)
	{
		pRet->setUserTag(pRet->getEntrustID());
	}
	else
	{
		pRet->setUserTag(usertag);

		if (strlen(pRet->getOrderID()) > 0)
		{
			m_oidCache.put(StrUtil::trim(pRet->getOrderID()).c_str(), usertag, 0, [this](const char* message) {
				write_log(_sink, LL_ERROR, message);
			});
		}
	}

	return pRet;
}

WTSTradeInfo* TraderATP::makeTradeInfo(const APITradeOrderUnit* trade_info)
{
	std::string code, exchg;
	if (trade_info->market_id == 101)
		exchg = "SSE";
	else if (trade_info->market_id == 102)
		exchg = "SZSE";

	code = trade_info->security_id;
	WTSContractInfo* contract = _bd_mgr->getContract(code.c_str(), exchg.c_str());
	if (contract == NULL)
		return NULL;

	WTSTradeInfo *pRet = WTSTradeInfo::create(code.c_str(), exchg.c_str());
	pRet->setVolume(trade_info->last_qty / 100.0);
	pRet->setPrice(trade_info->last_px / 10000.0);
	pRet->setTradeID(trade_info->exec_id);
	pRet->setAmount(trade_info->total_value_traded / 100.0);
	pRet->setContractInfo(contract);

	uint32_t uTime = (uint32_t)(trade_info->transact_time % 1000000000);
	uint32_t uDate = (uint32_t)(trade_info->transact_time / 1000000000);

	pRet->setTradeDate(uDate);
	pRet->setTradeTime(TimeUtils::makeTime(uDate, uTime));

	WTSDirectionType dType = wrapATPSide(trade_info->side);
	pRet->setDirection(dType);

	pRet->setOffsetType(wrapOffsetType(trade_info->side));
	pRet->setRefOrder(fmt::format("{}", trade_info->order_id).c_str());
	pRet->setTradeType(WTT_Common);

	const char* usertag = m_oidCache.get(StrUtil::trim(pRet->getRefOrder()).c_str());
	if (strlen(usertag))
		pRet->setUserTag(usertag);

	return pRet;
}

void TraderATP::OnLogin(const std::string& reason)
{
	if (_sink)
		_sink->handleEvent(WTE_Connect, 0);

	_state = TS_LOGINED;  // 已登录
	write_log(_sink, LL_WARN, "[TraderATP] {} login success: {}...", _user, reason);
}

void TraderATP::OnLogout(const std::string& reason)
{
	if (_sink)
		_sink->handleEvent(WTE_Logout, 0);

	_state = TS_NOTLOGIN;
	write_log(_sink, LL_WARN, "[TraderATP] {} logout: {}...", _user, reason);
}

void TraderATP::OnConnectFailure(const std::string& reason)
{
	if (_sink)
		_sink->handleEvent(WTE_Connect, -1);

	_state = TS_LOGINFAILED;
	write_log(_sink, LL_WARN, "[TraderATP] Connect failed {}...", reason);
}

void TraderATP::OnConnectTimeOut(const std::string& reason)
{
	if (_sink)
		_sink->handleEvent(WTE_Connect, -1);

	_state = TS_LOGINFAILED;
	write_log(_sink, LL_WARN, "[TraderATP] Connect timeout {}...", reason);
}

void TraderATP::OnHeartbeatTimeout(const std::string& reason)
{
	write_log(_sink, LL_WARN, "[TraderATP] Heartbeat timeout {}...", reason);
}

void TraderATP::OnClosed(const std::string& reason)
{
	_state = TS_NOTLOGIN;
	write_log(_sink, LL_WARN, "[TraderATP] Server closed {}...", reason);
}

void TraderATP::OnEndOfConnection(const std::string& reason)
{
	if (_sink)
		_sink->handleEvent(WTE_Close, -1);

	write_log(_sink, LL_WARN, "[TraderATP] Connection ended {}...", reason);

	_close_flag = true;
}

void TraderATP::OnRspCustLoginResp(const ATPRspCustLoginRespOtherMsg &cust_login_resp)
{
	std::cout << "OnRspCustLoginResp Recv:" << static_cast<uint32_t>(cust_login_resp.permisson_error_code) << std::endl;

	std::cout << "cust_login_resp : " << std::endl;
	std::cout << "client_seq_id : " << cust_login_resp.client_seq_id <<
		" transact_time : " << cust_login_resp.transact_time <<
		" cust_id : " << cust_login_resp.cust_id <<
		" permisson_error_code : " << cust_login_resp.permisson_error_code <<
		" user_info : " << cust_login_resp.user_info << std::endl;
	//std::vector<APIMsgCustLoginRespFundAccountUnit>::iterator it;
	//std::vector<APIMsgCustLoginRespAccountUnit>::iterator it_1;
	int i = 1, j = 1;
	for (auto it = cust_login_resp.fund_account_array.begin();
		it != cust_login_resp.fund_account_array.end(); it++)
	{
		std::cout << " fund_account_array_" << i << " : " << std::endl;
		std::cout << " branch_id : " << it->fund_account_id <<
			" fund_account_id : " << it->branch_id << std::endl;
		for (auto it_1 = it->account_array.begin();
			it_1 != it->account_array.end(); it_1++)
		{
			std::cout << " account_array_" << i << "_" << j << " : " << std::endl;
			std::cout << " account_id : " << it_1->account_id <<
				" market_id : " << it_1->market_id << std::endl;

			j++;
		}
		i++;
	}

	if (cust_login_resp.permisson_error_code == 0)
	{
		_tradingday = TimeUtils::getCurDate();
		_cust_id = cust_login_resp.cust_id;

		{
			//初始化委托单缓存器
			std::stringstream ss;
			ss << "./atpdata/local/";
			std::string path = StrUtil::standardisePath(ss.str());
			if (!StdFile::exists(path.c_str()))
				boost::filesystem::create_directories(path.c_str());
			ss << _user << "_eid.sc";
			m_eidCache.init(ss.str().c_str(), _tradingday, [this](const char* message) {
				write_log(_sink, LL_WARN, message);
			});
		}

		{
			//初始化订单标记缓存器
			std::stringstream ss;
			ss << "./atpdata/local/";
			std::string path = StrUtil::standardisePath(ss.str());
			if (!StdFile::exists(path.c_str()))
				boost::filesystem::create_directories(path.c_str());
			ss << _user << "_oid.sc";
			m_oidCache.init(ss.str().c_str(), _tradingday, [this](const char* message) {
				write_log(_sink, LL_WARN, message);
			});
		}

		if (_sink)
			_sink->onLoginResult(true, "", _tradingday);
		_state = TS_ALLREADY;

		std::cout << "CustLogin Success!" << std::endl;
	}
	else
	{
		if (_sink)
			_sink->onLoginResult(false, fmt::format("{}", cust_login_resp.permisson_error_code).c_str(), _tradingday);
		_state = TS_ALLREADY;

		std::cout << "CustLogin Fail, permisson_error_code :" << static_cast<uint32_t>(cust_login_resp.permisson_error_code) << std::endl;
	}
}

void TraderATP::OnRspCustLogoutResp(const ATPRspCustLogoutRespOtherMsg &cust_logout_resp)
{
	std::cout << "OnRspCustLogoutResp Recv:" << static_cast<uint32_t>(cust_logout_resp.permisson_error_code) << std::endl;

	if (cust_logout_resp.permisson_error_code == 0)
	{
		_state = TS_NOTLOGIN;
		if (_sink)
			_sink->onLogout();

		std::cout << "CustLogout Success!" << std::endl;
	}
	else
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Logout failed: {}", static_cast<uint32_t>(cust_logout_resp.permisson_error_code));
	}
}

// 订单下达内部响应
void TraderATP::OnRspOrderStatusInternalAck(const ATPRspOrderStatusAckMsg& order_status_ack)
{
	std::cout << "order_status_ack : " << std::endl;
	std::cout << "partition : " << (int32_t)order_status_ack.partition <<
		" index : " << order_status_ack.index <<
		" business_type : " << (int32_t)order_status_ack.business_type <<
		" cl_ord_no : " << order_status_ack.cl_ord_no <<
		" security_id : " << order_status_ack.security_id <<
		" market_id : " << order_status_ack.market_id <<
		" exec_type : " << order_status_ack.exec_type <<
		" ord_status : " << (int32_t)order_status_ack.ord_status <<
		" cust_id : " << order_status_ack.cust_id <<
		" fund_account_id : " << order_status_ack.fund_account_id <<
		" account_id : " << order_status_ack.account_id <<
		" price : " << order_status_ack.price <<
		" order_qty : " << order_status_ack.order_qty <<
		" leaves_qty : " << order_status_ack.leaves_qty <<
		" cum_qty : " << order_status_ack.cum_qty <<
		" side : " << order_status_ack.side <<
		" transact_time : " << order_status_ack.transact_time <<
		" user_info : " << order_status_ack.user_info <<
		" order_id : " << order_status_ack.order_id <<
		" cl_ord_id : " << order_status_ack.cl_ord_id <<
		" client_seq_id : " << order_status_ack.client_seq_id <<
		" orig_cl_ord_no : " << order_status_ack.orig_cl_ord_no <<
		" frozen_trade_value : " << order_status_ack.frozen_trade_value <<
		" frozen_fee : " << order_status_ack.frozen_fee <<
		" reject_reason_code : " << order_status_ack.reject_reason_code <<
		" ord_rej_reason : " << order_status_ack.ord_rej_reason <<
		" order_type : " << order_status_ack.order_type <<
		" time_in_force : " << order_status_ack.time_in_force <<
		" position_effect : " << order_status_ack.position_effect <<
		" covered_or_uncovered : " << (int32_t)order_status_ack.covered_or_uncovered <<
		" account_sub_code : " << order_status_ack.account_sub_code << std::endl;

	// 保存回报分区号、序号，用于断线重连时指定已收到最新回报序号
	report_sync[order_status_ack.partition] = order_status_ack.index;
}

// 订单下达交易所确认
void TraderATP::OnRspOrderStatusAck(const ATPRspOrderStatusAckMsg& order_status_ack)
{
	std::cout << "order_status_ack : " << std::endl;
	std::cout << "partition : " << (int32_t)order_status_ack.partition <<
		" index : " << order_status_ack.index <<
		" business_type : " << (int32_t)order_status_ack.business_type <<
		" cl_ord_no : " << order_status_ack.cl_ord_no <<
		" security_id : " << order_status_ack.security_id <<
		" market_id : " << order_status_ack.market_id <<
		" exec_type : " << order_status_ack.exec_type <<
		" ord_status : " << (int32_t)order_status_ack.ord_status <<
		" cust_id : " << order_status_ack.cust_id <<
		" fund_account_id : " << order_status_ack.fund_account_id <<
		" account_id : " << order_status_ack.account_id <<
		" price : " << order_status_ack.price <<
		" order_qty : " << order_status_ack.order_qty <<
		" leaves_qty : " << order_status_ack.leaves_qty <<
		" cum_qty : " << order_status_ack.cum_qty <<
		" side : " << order_status_ack.side <<
		" transact_time : " << order_status_ack.transact_time <<
		" user_info : " << order_status_ack.user_info <<
		" order_id : " << order_status_ack.order_id <<
		" cl_ord_id : " << order_status_ack.cl_ord_id <<
		" client_seq_id : " << order_status_ack.client_seq_id <<
		" orig_cl_ord_no : " << order_status_ack.orig_cl_ord_no <<
		" frozen_trade_value : " << order_status_ack.frozen_trade_value <<
		" frozen_fee : " << order_status_ack.frozen_fee <<
		" reject_reason_code : " << order_status_ack.reject_reason_code <<
		" ord_rej_reason : " << order_status_ack.ord_rej_reason <<
		" order_type : " << order_status_ack.order_type <<
		" time_in_force : " << order_status_ack.time_in_force <<
		" position_effect : " << order_status_ack.position_effect <<
		" covered_or_uncovered : " << (int32_t)order_status_ack.covered_or_uncovered <<
		" account_sub_code : " << order_status_ack.account_sub_code <<
		" quote_flag:" << (int32_t)order_status_ack.quote_flag << std::endl;

	// 保存回报分区号、序号，用于断线重连时指定已收到最新回报序号
	report_sync[order_status_ack.partition] = order_status_ack.index;
}

// 成交回报
void TraderATP::OnRspCashAuctionTradeER(const ATPRspCashAuctionTradeERMsg& cash_auction_trade_er)
{
	std::cout << "cash_auction_trade_er : " << std::endl;
	std::cout << "partition : " << (int32_t)cash_auction_trade_er.partition <<
		" index : " << cash_auction_trade_er.index <<
		" business_type : " << (int32_t)cash_auction_trade_er.business_type <<
		" cl_ord_no : " << cash_auction_trade_er.cl_ord_no <<
		" security_id : " << cash_auction_trade_er.security_id <<
		" market_id : " << cash_auction_trade_er.market_id <<
		" exec_type : " << cash_auction_trade_er.exec_type <<
		" ord_status : " << (int32_t)cash_auction_trade_er.ord_status <<
		" cust_id : " << cash_auction_trade_er.cust_id <<
		" fund_account_id : " << cash_auction_trade_er.fund_account_id <<
		" account_id : " << cash_auction_trade_er.account_id <<
		" price : " << cash_auction_trade_er.price <<
		" order_qty : " << cash_auction_trade_er.order_qty <<
		" leaves_qty : " << cash_auction_trade_er.leaves_qty <<
		" cum_qty : " << cash_auction_trade_er.cum_qty <<
		" side : " << cash_auction_trade_er.side <<
		" transact_time : " << cash_auction_trade_er.transact_time <<
		" user_info : " << cash_auction_trade_er.user_info <<
		" order_id : " << cash_auction_trade_er.order_id <<
		" cl_ord_id : " << cash_auction_trade_er.cl_ord_id <<
		" exec_id : " << cash_auction_trade_er.exec_id <<
		" last_px : " << cash_auction_trade_er.last_px <<
		" last_qty : " << cash_auction_trade_er.last_qty <<
		" total_value_traded : " << cash_auction_trade_er.total_value_traded <<
		" fee : " << cash_auction_trade_er.fee <<
		" cash_margin : " << cash_auction_trade_er.cash_margin << std::endl;

	// 保存回报分区号、序号，用于断线重连时指定已收到最新回报序号
	report_sync[cash_auction_trade_er.partition] = cash_auction_trade_er.index;
}

// 订单下达内部拒绝
void TraderATP::OnRspBizRejection(const ATPRspBizRejectionOtherMsg& biz_rejection)
{
	std::cout << "biz_rejection : " << std::endl;
	std::cout << " transact_time : " << biz_rejection.transact_time <<
		" client_seq_id : " << biz_rejection.client_seq_id <<
		" msg_type : " << biz_rejection.api_msg_type <<
		" reject_reason_code : " << biz_rejection.reject_reason_code <<
		" business_reject_text : " << biz_rejection.business_reject_text <<
		" user_info : " << biz_rejection.user_info << std::endl;

	WTSError* error = WTSError::create(WEC_ORDERCANCEL, biz_rejection.business_reject_text);
	if (_sink)
		_sink->onTraderError(error);
}

void TraderATP::OnRspFundQueryResult(const ATPRspFundQueryResultMsg &msg)
{
	std::cout << "fund_query_result : " << std::endl;
	std::cout << "cust_id : " << msg.cust_id <<
		" fund_account_id : " << msg.fund_account_id <<
		" account_id : " << msg.account_id <<
		" client_seq_id : " << msg.client_seq_id <<
		" query_result_code : " << msg.query_result_code <<
		" user_info : " << msg.user_info <<
		" leaves_value : " << msg.leaves_value <<
		" init_leaves_value : " << msg.init_leaves_value <<
		" available_t0 : " << msg.available_t0 <<
		" available_t1 : " << msg.available_t1 <<
		" available_t2 : " << msg.available_t2 <<
		" available_t3 : " << msg.available_t3 <<
		" available_tall : " << msg.available_tall <<
		" frozen_all : " << msg.frozen_all <<
		" te_partition_no : " << (int32_t)msg.te_partition_no <<
		" credit_sell_frozen : " << msg.credit_sell_frozen << std::endl;

	WTSArray* ayFunds = WTSArray::create();
	WTSAccountInfo* fundInfo = WTSAccountInfo::create();
	fundInfo->setPreBalance(msg.init_leaves_value / 10000.0);
	fundInfo->setBalance(msg.leaves_value / 10000.0);
	fundInfo->setAvailable(msg.available_tall / 10000.0);
	ayFunds->append(fundInfo, false);

	if (_sink)
		_sink->onRspAccount(ayFunds);

	ayFunds->release();
}

void TraderATP::OnRspOrderQueryResult(const ATPRspOrderQueryResultMsg &msg)
{
	WTSArray* ayOrders = WTSArray::create();

	int i = 1;
	for (const auto& unit : msg.order_array)
	{
		std::cout << " order_array_" << i << " : " << std::endl;
		std::cout << " order_array_" << i << " : " << std::endl;
		std::cout << " business_type : " << (int32_t)unit.business_type <<
			" security_id : " << unit.security_id <<
			" security_symbol : " << unit.security_symbol <<
			" market_id : " << unit.market_id <<
			" account_id : " << unit.account_id <<
			" side : " << unit.side <<
			" ord_type : " << unit.ord_type <<
			" ord_status : " << (int32_t)unit.ord_status <<
			" transact_time : " << unit.transact_time <<
			" order_price : " << unit.order_price <<
			" exec_price : " << unit.exec_price <<
			" order_qty : " << unit.order_qty <<
			" leaves_qty : " << unit.leaves_qty <<
			" cum_qty : " << unit.cum_qty <<
			" cl_ord_no : " << unit.cl_ord_no <<
			" order_id : " << unit.order_id <<
			" cl_ord_id : " << unit.cl_ord_id <<
			" client_seq_id : " << unit.client_seq_id <<
			" orig_cl_ord_no : " << unit.orig_cl_ord_no <<
			" frozen_trade_value : " << unit.frozen_trade_value <<
			" frozen_fee : " << unit.frozen_fee <<
			" reject_reason_code : " << unit.reject_reason_code <<
			" ord_rej_reason : " << unit.ord_rej_reason << std::endl;
		i++;

		WTSOrderInfo* ordInfo = makeOrderInfo(&unit);
		if (ordInfo == NULL)
			continue;

		ayOrders->append(ordInfo, false);
	}

	if (_sink)
		_sink->onRspOrders(ayOrders);

	ayOrders->release();
}

void TraderATP::OnRspTradeOrderQueryResult(const ATPRspTradeOrderQueryResultMsg &msg)
{
	WTSArray* ayTrades = WTSArray::create();

	int i = 1;
	for (const auto& unit : msg.order_array)
	{
		std::cout << " order_array_" << i << " : " << std::endl;
		std::cout << " business_type : " << (int32_t)unit.business_type <<
			" security_id : " << unit.security_id <<
			" security_symbol : " << unit.security_symbol <<
			" market_id : " << unit.market_id <<
			" account_id : " << unit.account_id <<
			" side : " << unit.side <<
			" ord_type : " << unit.ord_type <<
			" transact_time : " << unit.transact_time <<
			" order_qty : " << unit.order_qty <<
			" cl_ord_no : " << unit.cl_ord_no <<
			" order_id : " << unit.order_id <<
			" cl_ord_id : " << unit.cl_ord_id <<
			" client_seq_id : " << unit.client_seq_id << std::endl;
		i++;

		WTSTradeInfo* trdInfo = makeTradeInfo(&unit);
		if (trdInfo == NULL)
			continue;

		ayTrades->append(trdInfo, false);
	}

	if (_sink)
		_sink->onRspTrades(ayTrades);

	ayTrades->release();
}

void TraderATP::OnRspShareQueryResult(const ATPRspShareQueryResultMsg &msg)
{
	std::cout << "share_query_result : " << std::endl;
	std::cout << "cust_id : " << msg.cust_id <<
		" fund_account_id : " << msg.fund_account_id <<
		" account_id : " << msg.account_id <<
		" client_seq_id : " << msg.client_seq_id <<
		" query_result_code : " << msg.query_result_code <<
		" user_info : " << msg.user_info <<
		" last_index : " << msg.last_index <<
		" total_num : " << msg.total_num << std::endl;

	//std::vector<APIShareUnit>::iterator it;
	if (NULL == _positions)
		_positions = PositionMap::create();
	int i = 1;

	for (auto &unit : msg.order_array) {
		std::cout << " order_array_" << i << " : " << std::endl;
		std::cout << " security_id : " << unit.security_id <<
			" security_symbol : " << unit.security_symbol <<
			" market_id : " << unit.market_id <<
			" account_id : " << unit.account_id <<
			" init_qty : " << unit.init_qty <<
			" leaves_qty : " << unit.leaves_qty <<
			" available_qty : " << unit.available_qty <<
			" profit_loss : " << unit.profit_loss <<
			" market_value : " << unit.market_value <<
			" cost_price : " << unit.cost_price << std::endl;
		i++;

		std::string exchg;
		if (unit.market_id == ATPMarketIDConst::kShangHai)
			exchg = "SSE";
		else
			exchg = "SZSE";

		WTSContractInfo* contract = _bd_mgr->getContract(unit.security_id, exchg.c_str());
		if (contract)
		{
			WTSCommodityInfo* commInfo = contract->getCommInfo();
			std::string key = fmt::format("{}-{}", unit.security_id, exchg.c_str());
			WTSPositionItem* pos = (WTSPositionItem*)_positions->get(key);
			if (pos == NULL)
			{
				pos = WTSPositionItem::create(unit.security_id, commInfo->getCurrency(), commInfo->getExchg());
				pos->setContractInfo(contract);
				_positions->add(key, pos, false);
			}
			//pos->setDirection(wrapPosDirection(position->position_direction));

			pos->setNewPosition((double)(unit.available_qty / 100.0));
			pos->setPrePosition((double)unit.init_qty / 100.0);

			pos->setMargin(unit.market_value / 10000.0);
			pos->setDynProfit(unit.profit_loss / 10000.0);
			pos->setPositionCost(unit.market_value / 10000.0);

			pos->setAvgPrice(unit.cost_price / 100.0);

			pos->setAvailNewPos(0);
			pos->setAvailPrePos((double)unit.init_qty / 100.0);
		}
	}

	WTSArray* ayPos = WTSArray::create();

	if (_positions && _positions->size() > 0)
	{
		for (auto it = _positions->begin(); it != _positions->end(); it++)
		{
			ayPos->append(it->second, true);
		}
	}

	if (_sink)
		_sink->onRspPosition(ayPos);

	if (_positions)
	{
		_positions->release();
		_positions = NULL;
	}

	ayPos->release();
}

#pragma region "ITraderApi"
bool TraderATP::init(WTSVariant *params)
{
	write_log(_sink, LL_INFO, "Initalizing TraderATP ...");

	_user = params->getCString("user");
	_pass = params->getCString("pass");
	_accountid = params->getCString("account_id");
	_accpasswd = params->getCString("account_key");
	_fund_accountid = params->getCString("fund_account_id");
	_cust_id = params->getCString("cust_id");

	_front = params->getCString("front");
	_front2 = params->getCString("front_backup");

	write_log(_sink, LL_INFO, "user: {} | password: {} | account_id: {} | account_password: {} | ip_host: {}", _user, _pass, _accountid, _accpasswd, _front);

	std::string module = params->getCString("atpmodule");
	if (module.empty()) module = "atptradeapi";
	std::string dllpath = getBinDir() + DLLHelper::wrap_module(module.c_str(), "lib");;
	m_hInstATP = DLLHelper::load_library(dllpath.c_str());

	static bool inited = false;
	if (!inited)
	{
		// 初始化API
		const std::string station_name = ""; // 站点信息，该字段已经不使用
		const std::string cfg_path = ".";      // 配置文件路径
		const std::string log_dir_path = ""; // 日志路径
		bool record_all_flag = true;         // 是否记录所有委托信息
		std::unordered_map<std::string, std::string> encrypt_cfg; // 加密库配置
		bool connection_retention_flag = false;   // 是否启用会话保持

		// encrypt_cfg参数填写：
		encrypt_cfg["ENCRYPT_SCHEMA"] = "0";              // 字符 0 表示 不对消息中的所有 password 加密
		encrypt_cfg["ATP_ENCRYPT_PASSWORD"] = "";         // 除登入及密码修改外其他消息的密码字段加密算法
		encrypt_cfg["ATP_LOGIN_ENCRYPT_PASSWORD"] = "";   // 登入及密码修改消息中密码字段的加密算法so路径
		encrypt_cfg["GM_SM2_PUBLIC_KEY_PATH"] = "";       // 采用国密算法时，通过该key配置 GM算法配置加密使用的公钥路径
		encrypt_cfg["RSA_PUBLIC_KEY_PATH"] = "";          // 如果使用rsa算法加密，通过该key配置 rsa算法配置加密使用的公钥路径

		ATPRetCodeType ec = ATPTradeAPI::Init(station_name, cfg_path, log_dir_path, record_all_flag, encrypt_cfg, connection_retention_flag);

		if (ec != ErrorCode::kSuccess)
		{
			write_log(_sink, LL_ERROR, "ATPTradeAPI init failed: {}", ec);
			return false;
		}

		inited = true;
	}

	return true;
}

void TraderATP::release()
{
	if (_api)
	{
		_api->Close();
		while (!_close_flag)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		delete _api;
		_api = NULL;
	}

	if (_orders)
		_orders->clear();

	if (_positions)
		_positions->clear();

	if (_trades)
		_trades->clear();
}

void TraderATP::registerSpi(ITraderSpi *listener)
{
	_sink = listener;
	if (_sink)
	{
		_bd_mgr = listener->getBaseDataMgr();
	}
}

void TraderATP::reconnect()
{
	if (_api)
	{
		_api->Close();
		while (!_close_flag)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
		delete _api;
		_api = NULL;
	}

	_api = new ATPTradeAPI();

	std::vector<std::string> locations;
	locations.push_back(_front);
	locations.push_back(_front2);

	// 设置连接信息
	ATPConnectProperty prop;
	prop.user = _user;												// 网关用户名
	prop.password = _pass;											// 网关用户密码
	prop.locations = locations;										// 网关主备节点的地址+端口
	prop.heartbeat_interval_milli = 5000;                           // 发送心跳的时间间隔，单位：毫秒
	prop.connect_timeout_milli = 5000;                              // 连接超时时间，单位：毫秒
	prop.reconnect_time = 10;                                       // 重试连接次数
	prop.client_name = "WonderTrader";                              // 客户端程序名字
	prop.client_version = "V1.0.0";									// 客户端程序版本
	prop.report_sync = report_sync;									// 回报同步数据分区号+序号，首次是空，断线重连时填入的是接受到的最新分区号+序号
	prop.mode = 0;													// 模式0-同步回报模式，模式1-快速登录模式，不同步回报

	ATPRetCodeType ec = _api->Connect(prop, this);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Connect error: {}", ec);
	}
}

void TraderATP::connect()
{
	write_log(_sink, LL_INFO, "Connecting to server, user_info: {}, password: {}, account_id: {}", _user, _pass, _accountid);

	reconnect();

	if (_thrd_worker == NULL)
	{
		boost::asio::io_service::work work(_asyncio);
		_thrd_worker.reset(new StdThread([this]() {
			while (true)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				_asyncio.run_one();
				//m_asyncIO.run();
			}
		}));
	}
}

void TraderATP::disconnect()
{
	release();
}

bool TraderATP::isConnected()
{
	return (_state == TS_ALLREADY);
}

void TraderATP::genEntrustID(char* buffer, uint32_t orderRef)
{
	//这里不再使用sessionid，因为每次登陆会不同，如果使用的话，可能会造成不唯一的情况
	fmtutil::format_to(buffer, "{}#{}#{}", _user, _tradingday, orderRef);
}

bool TraderATP::extractEntrustID(const char* entrustid, uint32_t &orderRef)
{
	auto idx = StrUtil::findLast(entrustid, '#');
	if (idx == std::string::npos)
		return false;

	orderRef = strtoul(entrustid + idx + 1, NULL, 10);

	return true;
}

bool TraderATP::makeEntrustID(char* buffer, int length)
{
	if (buffer == NULL || length == 0)
		return false;

	try
	{
		uint32_t orderref = _ordref.fetch_add(1) + 1;
		fmtutil::format_to(buffer, "{}#{}#{}", _user, _tradingday, orderref);
		return true;
	}
	catch (...)
	{

	}

	return false;
}

void TraderATP::doLogin(const char* productInfo)
{
	write_log(_sink, LL_INFO, "Now logging in ...");

	_state = TS_LOGINING;

	// 设置登入消息
	ATPReqCustLoginOtherMsg login_msg;
	wt_strcpy(login_msg.fund_account_id, _fund_accountid.c_str());           // 资金账户ID
	wt_strcpy(login_msg.password, _accpasswd.c_str());                  // 客户号密码
	strncpy(login_msg.user_info, _user.c_str(), 17);
	strncpy(login_msg.account_id, _accountid.c_str(), 17);
	login_msg.login_mode = ATPCustLoginModeType::kFundAccountIDMode;	// 登录模式，资金账号登录
	login_msg.client_seq_id = genRequestID();							// 客户系统消息号
	login_msg.order_way = '0';											// 委托方式，自助委托
	login_msg.client_feature_code = productInfo;						// 终端识别码
	strncpy(login_msg.branch_id, "00000", 11);

	ATPRetCodeType ec = _api->ReqCustLoginOther(&login_msg);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderAPT] Login error: {}", ec);
	}
}

int TraderATP::login(const char* user, const char* pass, const char* productInfo)
{
	_user = user;
	_pass = pass;
	_product = productInfo;

	if (_api == NULL)
	{
		return -1;
	}

	doLogin(productInfo);

	return 0;
}

int TraderATP::logout()
{
	if (_api == NULL)
		return -1;

	ATPReqCustLogoutOtherMsg logout_msg;
	wt_strcpy(logout_msg.fund_account_id, _accountid.c_str());	// 资金账户ID
	logout_msg.client_seq_id = genRequestID();              // 客户系统消息号
	logout_msg.client_feature_code = _product;				// 终端识别码

	ATPRetCodeType ec = _api->ReqCustLogoutOther(&logout_msg);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderAPT] Logout error: {}", ec);
		return ec;
	}
	return 0;
}

int TraderATP::orderInsert(WTSEntrust* entrust)
{
	if (_api == NULL || _state != TS_ALLREADY)
	{
		return -1;
	}

	//XTPOrderInsertInfo req;
	//memset(&req, 0, sizeof(req));
	//
	//uint32_t orderref;
	//extractEntrustID(entrust->getEntrustID(), orderref);
	//req.order_client_id = orderref;
	//strcpy(req.ticker, entrust->getCode());
	//req.market = wt_stricmp(entrust->getExchg(), "SSE") == 0 ? XTP_MKT_SH_A : XTP_MKT_SZ_A;
	//req.price = entrust->getPrice();
	//req.quantity = (int64_t)entrust->getVolume();
	//req.price_type = XTP_PRICE_LIMIT;
	//req.side = wrapDirectionType(entrust->getDirection(), entrust->getOffsetType());
	//req.business_type = XTP_BUSINESS_TYPE_CASH;
	//req.position_effect = wrapOffsetType(entrust->getOffsetType());

	//if (strlen(entrust->getUserTag()) > 0)
	//{
	//	m_eidCache.put(entrust->getEntrustID(), entrust->getUserTag(), 0, [this](const char* message) {
	//		write_log(_sink, LL_WARN, message);
	//	});
	//}

	//uint64_t iResult = _api->InsertOrder(&req, _sessionid);
	//if (iResult == 0)
	//{
	//	auto error_info = _api->GetApiLastError();
	//	write_log(_sink,LL_ERROR, "[TraderATP] Order inserting failed: {}", error_info->error_msg);
	//}


	// 上海市场股票限价委托
	thread_local static ATPReqCashAuctionOrderMsg p;

	uint32_t orderref;
	extractEntrustID(entrust->getEntrustID(), orderref);

	wt_strcpy(p.security_id, entrust->getCode());                   // 证券代码
	p.market_id = strcmp(entrust->getExchg(), "SSE") == 0 ? ATPMarketIDConst::kShangHai : ATPMarketIDConst::kShenZhen;             // 市场ID，上海
	wt_strcpy(p.cust_id, _cust_id.c_str());                 // 客户号ID
	wt_strcpy(p.fund_account_id, _fund_accountid.c_str());       // 资金账户ID
	wt_strcpy(p.account_id, _accountid.c_str());                 // 账户ID
	p.side = (entrust->getOffsetType() == WOT_OPEN) ? ATPSideConst::kBuy : ATPSideConst::kSell;     // 买卖方向，买
	p.order_type = ATPOrdTypeConst::kFixedNew;				// 订单类型，限价
	p.price = (int32_t)(entrust->getPrice() * 10000);         // 委托价格 N13(4)，21.0000元
	p.order_qty = (int32_t)(entrust->getVolume() * 100);            // 申报数量N15(2)；股票为股、基金为份、上海债券默认为张（使用时请务必与券商确认），其他为张；1000.00股
	p.client_seq_id = genRequestID();						// 用户系统消息序号
	p.order_way = '0';										// 委托方式，自助委托
	p.client_feature_code = _product;						// 终端识别码
	strncpy(p.password, _pass.c_str(), 129);							// 客户密码
	fmt::format_to(p.user_info, "{}", orderref);

	std::cout << "order info: " << std::endl;
	std::cout << "security id: " << p.security_id << std::endl;
	std::cout << "market id: " << p.market_id << std::endl;
	std::cout << "cust id: " << p.cust_id << std::endl;
	std::cout << "account id: " << p.account_id << std::endl;
	std::cout << "order_type: " << p.order_type << std::endl;
	std::cout << "side id: " << p.side << std::endl;
	std::cout << "order price: " << p.price << std::endl;
	std::cout << "order quantity: " << p.order_qty << std::endl;

	if (strlen(entrust->getUserTag()) > 0)
	{
		m_eidCache.put(entrust->getEntrustID(), entrust->getUserTag(), 0, [this](const char* message) {
			write_log(_sink, LL_WARN, message);
		});
	}

	ATPRetCodeType ec = _api->ReqCashAuctionOrder(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Order inserting failed: {}", ec);
	}

	return ec;
}

int TraderATP::orderAction(WTSEntrustAction* action)
{
	if (_api == NULL || _state != TS_ALLREADY)
	{
		return -1;
	}

	thread_local static ATPReqCancelOrderMsg p;
	p.market_id = strcmp(action->getExchg(), "SSE") == 0 ? ATPMarketIDConst::kShangHai : ATPMarketIDConst::kShenZhen;
	wt_strcpy(p.cust_id, _cust_id.c_str());                    // 客户号ID
	wt_strcpy(p.fund_account_id, _fund_accountid.c_str());          // 资金账户ID
	wt_strcpy(p.user_info, _user.c_str());                   // 账户ID
	wt_strcpy(p.account_id, _accountid.c_str());  // 证券账户
	wt_strcpy(p.password, _accpasswd.c_str());  // 交易密码
	p.client_seq_id = genRequestID();
	p.order_way = '0';
	p.orig_cl_ord_no = strtoull(action->getOrderID(), NULL, 10);

	std::cout << "send seq_id of order : " << p.client_seq_id << std::endl;

	ATPRetCodeType ec = _api->ReqCancelOrder(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Order cancelling failed: {}", ec);
	}

	return 0;
}

uint32_t TraderATP::genRequestID()
{
	return _reqid.fetch_add(1) + 1;
}

int TraderATP::queryAccount()
{
	ATPReqFundQueryMsg p;

	strncpy(p.cust_id, _cust_id.c_str(), 17);
	strncpy(p.fund_account_id, _fund_accountid.c_str(), 17);
	strncpy(p.account_id, _accountid.c_str(), 13);
	p.client_seq_id = genRequestID();
	strncpy(p.user_info, _user.c_str(), 17);
	strncpy(p.password, _pass.c_str(), 129);
	strncpy(p.currency, "CNY", 5);

	std::cout << "send seq_id of order : " << p.client_seq_id << std::endl;
	std::cout << "cust_id: " << p.cust_id << " fund_account_id: " << p.fund_account_id
		<< " account_id: " << p.account_id << " user: " << p.user_info << std::endl;

	ATPRetCodeType ec = _api->ReqFundQuery(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Query account failed: {}", ec);
	}
	return 0;
}

int TraderATP::queryPositions()
{
	ATPReqShareQueryMsg p;
	strncpy(p.cust_id, _cust_id.c_str(), 17);
	strncpy(p.fund_account_id, _fund_accountid.c_str(), 17);
	strncpy(p.account_id, _accountid.c_str(), 13);
	p.client_seq_id = genRequestID();
	strncpy(p.user_info, _user.c_str(), 17);
	strncpy(p.password, _pass.c_str(), 129);

	APIAccountIDUnit api_account_unit;
	strncpy(api_account_unit.account_id, _accountid.c_str(), 13);
	p.account_id_array.push_back(api_account_unit);

	//p->business_type = business_type;
	//p->market_id = market_id;
	//strncpy(p->security_id, security_id.c_str(), 9);
	//p->query_index = query_index;
	//p->return_num = return_num;

	std::cout << "send seq_id of order : " << p.client_seq_id << std::endl;

	ATPRetCodeType ec = _api->ReqShareQuery(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Query positions failed: {}", ec);
	}

	return 0;
}

int TraderATP::queryOrders()
{
	ATPReqOrderQueryMsg p;
	strncpy(p.cust_id, _cust_id.c_str(), 17);
	strncpy(p.fund_account_id, _fund_accountid.c_str(), 17);
	strncpy(p.account_id, _accountid.c_str(), 13);
	p.client_seq_id = genRequestID();
	p.business_abstract_type = 1;
	strncpy(p.user_info, _user.c_str(), 17);
	strncpy(p.password, _pass.c_str(), 129);

	std::cout << "send seq_id of order : " << p.client_seq_id << std::endl;

	ATPRetCodeType ec = _api->ReqOrderQuery(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Query orders failed: {}", ec);
	}

	return 0;
}

int TraderATP::queryTrades()
{
	ATPReqTradeOrderQueryMsg p;
	strncpy(p.cust_id, _cust_id.c_str(), 17);
	strncpy(p.fund_account_id, _fund_accountid.c_str(), 17);
	strncpy(p.account_id, _accountid.c_str(), 13);
	p.client_seq_id = genRequestID();
	p.business_abstract_type = 1;
	strncpy(p.user_info, _user.c_str(), 17);
	strncpy(p.password, _pass.c_str(), 129);

	std::cout << "send seq_id of order : " << p.client_seq_id << std::endl;

	ATPRetCodeType ec = _api->ReqTradeOrderQuery(&p);
	if (ec != ErrorCode::kSuccess)
	{
		write_log(_sink, LL_ERROR, "[TraderATP] Query trades failed: {}", ec);
	}

	return 0;
}
#pragma endregion "ITraderApi"
