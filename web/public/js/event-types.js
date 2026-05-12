/**
 * 事件類型常數（對齊 firmware/lib/ems_supp/ems_supp_model.h）
 * 整個 web/ 共用
 */

export const EVT = Object.freeze({
  EPI_LOCAL: 0x01,
  SHOCK_LOCAL: 0x02,
  AMIODARONE: 0x03,
  EPI_PRE_HANDOVER: 0x04,
  EPI_PURE_SUPP: 0x05,
  SHOCK_PRE_HANDOVER: 0x06,
  SHOCK_PURE_SUPP: 0x07,
});

export const EVT_LABEL = Object.freeze({
  [EVT.EPI_LOCAL]: "EPI（本機）",
  [EVT.SHOCK_LOCAL]: "電擊（本機）",
  [EVT.AMIODARONE]: "Amiodarone",
  [EVT.EPI_PRE_HANDOVER]: "EPI（接手前）",
  [EVT.EPI_PURE_SUPP]: "EPI（純補登）",
  [EVT.SHOCK_PRE_HANDOVER]: "電擊（接手前）",
  [EVT.SHOCK_PURE_SUPP]: "電擊（純補登）",
});

// 補登事件無時間戳，Timeline 顯示「-」
export const SUPP_EVENT_TYPES = new Set([
  EVT.EPI_PRE_HANDOVER,
  EVT.EPI_PURE_SUPP,
  EVT.SHOCK_PRE_HANDOVER,
  EVT.SHOCK_PURE_SUPP,
]);

export function isSuppEvent(type) {
  return SUPP_EVENT_TYPES.has(type);
}

/**
 * 將事件分類為 EPI / 電擊 / Amiodarone 三大族（含本機+補登）
 */
export function eventCategory(type) {
  if (type === EVT.EPI_LOCAL || type === EVT.EPI_PRE_HANDOVER || type === EVT.EPI_PURE_SUPP) {
    return "epi";
  }
  if (type === EVT.SHOCK_LOCAL || type === EVT.SHOCK_PRE_HANDOVER || type === EVT.SHOCK_PURE_SUPP) {
    return "shock";
  }
  if (type === EVT.AMIODARONE) {
    return "amio";
  }
  return "other";
}
