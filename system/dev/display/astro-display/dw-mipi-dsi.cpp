// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dw-mipi-dsi.h"
#include <ddk/debug.h>
#include <ddktl/device.h>

namespace astro_display {

#define READ32_MIPI_DSI_REG(a)              mipi_dsi_mmio_->Read32(a)
#define WRITE32_MIPI_DSI_REG(a, v)          mipi_dsi_mmio_->Write32(v, a)


inline bool DwMipiDsi::IsPldREmpty() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_R_EMPTY, 1) == 1);
}

inline bool DwMipiDsi::IsPldRFull() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_R_FULL, 1) == 1);
}

inline bool DwMipiDsi::IsPldWEmpty() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_W_EMPTY, 1) == 1);
}

inline bool DwMipiDsi::IsPldWFull() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_W_FULL, 1) == 1);
}

inline bool DwMipiDsi::IsCmdEmpty() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_CMD_EMPTY, 1) == 1);
}

inline bool DwMipiDsi::IsCmdFull() {
    return (GET_BIT32(MIPI_DSI, DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_CMD_FULL, 1) == 1);
}

zx_status_t DwMipiDsi::WaitforFifo(uint32_t reg, uint32_t bit, uint32_t val) {
    int retry = MIPI_DSI_RETRY_MAX;
    while (GET_BIT32(MIPI_DSI, reg, bit, 1) != val && retry--) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    }
    if (retry <= 0) {
        return ZX_ERR_TIMED_OUT;
    }
    return ZX_OK;
}

zx_status_t DwMipiDsi::WaitforPldWNotFull() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_W_FULL, 0);
}

zx_status_t DwMipiDsi::WaitforPldWEmpty() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_W_EMPTY, 1);
}

zx_status_t DwMipiDsi::WaitforPldRFull() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_R_FULL, 1);
}

zx_status_t DwMipiDsi::WaitforPldRNotEmpty() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_PLD_R_EMPTY, 0);
}

zx_status_t DwMipiDsi::WaitforCmdNotFull() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_CMD_FULL, 0);
}

zx_status_t DwMipiDsi::WaitforCmdEmpty() {
    return WaitforFifo(DW_DSI_CMD_PKT_STATUS, CMD_PKT_STATUS_CMD_EMPTY, 1);
}

void DwMipiDsi::DumpCmd(const MipiDsiCmd& cmd) {
    zxlogf(ERROR, "\n\t\t MIPI DSI Command:\n");
    zxlogf(ERROR, "\t\t\t\t VIC = 0x%x (%d)\n", cmd.virt_chn_id, cmd.virt_chn_id);
    zxlogf(ERROR, "\t\t\t\t Data Type = 0x%x (%d)\n", cmd.dsi_data_type, cmd.dsi_data_type);
    zxlogf(ERROR, "\t\t\t\t ACK = 0x%x (%d)\n", cmd.flags, cmd.flags);
    zxlogf(ERROR, "\t\t\t\t Payload size = 0x%lx (%ld)\n", cmd.pld_size, cmd.pld_size);
    zxlogf(ERROR, "\t\t\t\t Payload Data: [");

    for (size_t i = 0; i < cmd.pld_size; i++) {
        zxlogf(ERROR, "0x%x, ", cmd.pld_data[i]);
    }
    zxlogf(ERROR, "]\n\n");
}

zx_status_t DwMipiDsi::GenericPayloadRead(uint32_t* data) {
    // make sure there is something valid to read from payload fifo
    if (WaitforPldRNotEmpty() != ZX_OK) {
        DISP_ERROR("Timeout! PLD R FIFO remained empty\n");
        return ZX_ERR_TIMED_OUT;
    }
    *data = READ32_REG(MIPI_DSI, DW_DSI_GEN_PLD_DATA);
    return ZX_OK;
}

zx_status_t DwMipiDsi::GenericHdrWrite(uint32_t data) {
    // make sure cmd fifo is not full before writing into it
    if (WaitforCmdNotFull() != ZX_OK) {
        DISP_ERROR("Timeout! CMD FIFO remained full\n");
        return ZX_ERR_TIMED_OUT;
    }
    WRITE32_REG(MIPI_DSI, DW_DSI_GEN_HDR, data);
    return ZX_OK;
}

zx_status_t DwMipiDsi::GenericPayloadWrite(uint32_t data) {
    // Make sure PLD_W is not full before writing into it
    if (WaitforPldWNotFull() != ZX_OK) {
        DISP_ERROR("Timeout! PLD W FIFO remained full!\n");
        return ZX_ERR_TIMED_OUT;
    }
    WRITE32_REG(MIPI_DSI, DW_DSI_GEN_PLD_DATA, data);
    return ZX_OK;
}

void DwMipiDsi::EnableBta() {
    // enable ack req after each packet transmission
    SET_BIT32(MIPI_DSI, DW_DSI_CMD_MODE_CFG,
        MIPI_DSI_ACK, CMD_MODE_CFG_ACK_RQST_EN, 1);
    // enable But Turn-Around request
    SET_BIT32(MIPI_DSI, DW_DSI_PCKHDL_CFG,
        MIPI_DSI_ACK, PCKHDL_CFG_BTA_EN, 1);
}

void DwMipiDsi::DisableBta() {
    // disable ack req after each packet transmission
    SET_BIT32(MIPI_DSI, DW_DSI_CMD_MODE_CFG,
        MIPI_DSI_NO_ACK, CMD_MODE_CFG_ACK_RQST_EN, 1);
    // disable But Turn-Around request
    SET_BIT32(MIPI_DSI, DW_DSI_PCKHDL_CFG,
        MIPI_DSI_NO_ACK, PCKHDL_CFG_BTA_EN, 1);
}

zx_status_t DwMipiDsi::WaitforBtaAck() {
    // BTA ACK is complete once Host PHY goes from RX to TX
    int retry;
    uint32_t phy_dir;

    // (1) TX --> RX
    retry = MIPI_DSI_RETRY_MAX;
    while (((phy_dir =
             GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_DIRECTION, 1)) == PHY_TX) &&
             retry--) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    }
    if (retry <= 0) {
        DISP_ERROR("Timeout! Phy Direction remained as TX\n");
        return ZX_ERR_TIMED_OUT;
    }

    // (2) RX --> TX
    retry = MIPI_DSI_RETRY_MAX;
    while (((phy_dir =
             GET_BIT32(MIPI_DSI, DW_DSI_PHY_STATUS, PHY_STATUS_PHY_DIRECTION, 1)) == PHY_RX) &&
             retry--) {
        zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    }
    if (retry <= 0) {
        DISP_ERROR("Timeout! Phy Direction remained as RX\n");
        return ZX_ERR_TIMED_OUT;
    }

    return ZX_OK;
}

// MIPI DSI Functions as implemented by DWC IP
zx_status_t DwMipiDsi::GenWriteShort(const MipiDsiCmd& cmd) {
    // Sanity check payload data and size
    if ((cmd.pld_size > 2) ||
        (cmd.pld_size > 0 && cmd.pld_data == NULL) ||
        (cmd.dsi_data_type & MIPI_DSI_DT_GEN_SHORT_WRITE_0) != MIPI_DSI_DT_GEN_SHORT_WRITE_0) {
        DISP_ERROR("Invalid Gen short cmd sent\n");
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t regVal = 0;
    regVal |= GEN_HDR_DT(cmd.dsi_data_type);
    regVal |= GEN_HDR_VC(cmd.virt_chn_id);
    if (cmd.pld_size >= 1) {
        regVal |= GEN_HDR_WC_LSB(cmd.pld_data[0]);
    }
    if (cmd.pld_size == 2) {
        regVal |= GEN_HDR_WC_MSB(cmd.pld_data[1]);
    }

    return GenericHdrWrite(regVal);
}

zx_status_t DwMipiDsi::DcsWriteShort(const MipiDsiCmd& cmd) {
    // Sanity check payload data and size
    if ((cmd.pld_size > 1) ||
        (cmd.pld_data == NULL) ||
        (cmd.dsi_data_type & MIPI_DSI_DT_DCS_SHORT_WRITE_0) != MIPI_DSI_DT_DCS_SHORT_WRITE_0) {
        DISP_ERROR("Invalid DCS short command\n");
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t regVal = 0;
    regVal |= GEN_HDR_DT(cmd.dsi_data_type);
    regVal |= GEN_HDR_VC(cmd.virt_chn_id);
    regVal |= GEN_HDR_WC_LSB(cmd.pld_data[0]);
    if (cmd.pld_size == 1) {
        regVal |= GEN_HDR_WC_MSB(cmd.pld_data[1]);
    }

    return GenericHdrWrite(regVal);
}

// This function writes a generic long command. We can only write a maximum of FIFO_DEPTH
// to the payload fifo. This value is implementation specific.
zx_status_t DwMipiDsi::GenWriteLong(const MipiDsiCmd& cmd) {
    zx_status_t status = ZX_OK;
    uint32_t pld_data_idx = 0; // payload data index
    uint32_t regVal = 0;
    ZX_DEBUG_ASSERT(cmd.pld_size < DWC_DEFAULT_MAX_PLD_FIFO_DEPTH);
    size_t ts = cmd.pld_size; // initial transfer size

    if (ts > 0 && cmd.pld_data == NULL) {
        DISP_ERROR("Invalid generic long write command\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // first write complete words
    while (ts >= 4) {
        regVal =    cmd.pld_data[pld_data_idx + 0] << 0  |
                    cmd.pld_data[pld_data_idx + 1] << 8  |
                    cmd.pld_data[pld_data_idx + 2] << 16 |
                    cmd.pld_data[pld_data_idx + 3] << 24;
        pld_data_idx += 4;
        if ((status = GenericPayloadWrite(regVal)) != ZX_OK) {
            DISP_ERROR("Generic Payload write failed! %d\n", status);
            return status;
        }
        ts -= 4;
    }

    // Write remaining bytes
    if (ts > 0) {
        regVal = cmd.pld_data[pld_data_idx++] << 0;
        if (ts > 1) {
            regVal |= cmd.pld_data[pld_data_idx++] << 8;
        }
        if (ts > 2) {
            regVal |= cmd.pld_data[pld_data_idx++] << 16;
        }
        if ((status = GenericPayloadWrite(regVal)) != ZX_OK) {
            DISP_ERROR("Generic Payload write failed! %d\n", status);
            return status;
        }
    }

    // At this point, we have written all of our mipi payload to FIFO. Let's transmit it
    regVal = 0;
    regVal |= GEN_HDR_DT(cmd.dsi_data_type);
    regVal |= GEN_HDR_VC(cmd.virt_chn_id);
    regVal |= GEN_HDR_WC_LSB(static_cast<uint32_t>(cmd.pld_size) & 0xFF);
    regVal |= GEN_HDR_WC_MSB((cmd.pld_size & 0xFF00) >> 16);

    return GenericHdrWrite(regVal);
}

zx_status_t DwMipiDsi::GenRead(const MipiDsiCmd& cmd) {
    uint32_t regVal = 0;
    zx_status_t status = ZX_OK;

    // valid cmd packet
    if ((cmd.rsp_data == NULL) || (cmd.pld_size > 2) ||
        (cmd.pld_size > 0 && cmd.pld_data == NULL)) {
        DISP_ERROR("Invalid generic read command\n");
        return ZX_ERR_INVALID_ARGS;
    }

    // Check whether max return packet size should be set
    if (cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) {
        // We will set the max return size as rlen
        regVal |= GEN_HDR_VC(cmd.virt_chn_id);
        regVal |= MIPI_DSI_DT_SET_MAX_RET_PKT;
        regVal |= GEN_HDR_WC_LSB(static_cast<uint32_t>(cmd.rsp_size) & 0xFF);
        regVal |= GEN_HDR_WC_MSB((static_cast<uint32_t>(cmd.rsp_size) >> 8) & 0xFF);

        if ((status = GenericHdrWrite(regVal)) != ZX_OK) {
            // no need to print extra info
            return status;
        }
    }

    regVal = 0;
    regVal |= GEN_HDR_DT(cmd.dsi_data_type);
    regVal |= GEN_HDR_VC(cmd.virt_chn_id);
    if (cmd.pld_size >= 1) {
        regVal |= GEN_HDR_WC_LSB(cmd.pld_data[0]);
    }
    if (cmd.pld_size == 2) {
        regVal |= GEN_HDR_WC_MSB(cmd.pld_data[1]);
    }

    // Packet is ready. Let's enable BTA first
    EnableBta();

    if ((status = GenericHdrWrite(regVal)) != ZX_OK) {
        // no need to print extra error msg
        return status;
    }

    if ((status = WaitforBtaAck()) != ZX_OK) {
        // bta never returned. no need to print extra error msg
        return status;
    }

    // Got ACK. Let's start reading
    // We should only read rlen worth of DATA. Let's hope the device is not sending
    // more than it should.
    size_t ts = cmd.rsp_size;
    uint32_t rsp_data_idx = 0;
    uint32_t data;
    while (ts >= 4) {
        if ((status = GenericPayloadRead(&data)) != ZX_OK) {
            DISP_ERROR("Something went wrong when reading data. Aborting\n");
            return status;
        }
        cmd.rsp_data[rsp_data_idx++] = static_cast<uint8_t>((data >> 0) & 0xFF);
        cmd.rsp_data[rsp_data_idx++] = static_cast<uint8_t>((data >> 8) & 0xFF);
        cmd.rsp_data[rsp_data_idx++] = static_cast<uint8_t>((data >> 16) & 0xFF);
        cmd.rsp_data[rsp_data_idx++] = static_cast<uint8_t>((data >> 24) & 0xFF);
        ts -= 4;
    }

    // Read out remaining bytes
    if (ts > 0) {
        if ((status = GenericPayloadRead(&data)) != ZX_OK) {
            DISP_ERROR("Something went wrong when reading data. Aborting\n");
            return status;
        }
        cmd.rsp_data[rsp_data_idx++] = (data >> 0) & 0xFF;
        if (ts > 1) {
            cmd.rsp_data[rsp_data_idx++] = (data >> 8) & 0xFF;
        }
        if (ts > 2) {
            cmd.rsp_data[rsp_data_idx++] = (data >> 16) & 0xFF;
        }
    }

    // we are done. Display BTA
    DisableBta();
    return status;
}

zx_status_t DwMipiDsi::SendCmd(const MipiDsiCmd& cmd) {

    zx_status_t status = ZX_OK;

    switch (cmd.dsi_data_type) {
    case MIPI_DSI_DT_GEN_SHORT_WRITE_0:
    case MIPI_DSI_DT_GEN_SHORT_WRITE_1:
    case MIPI_DSI_DT_GEN_SHORT_WRITE_2:
        status = GenWriteShort(cmd);
        break;
    case MIPI_DSI_DT_GEN_LONG_WRITE:
    case MIPI_DSI_DT_DCS_LONG_WRITE:
        status = GenWriteLong(cmd);
        break;
    case MIPI_DSI_DT_GEN_SHORT_READ_0:
    case MIPI_DSI_DT_GEN_SHORT_READ_1:
    case MIPI_DSI_DT_GEN_SHORT_READ_2:
        status = GenRead(cmd);
        break;
    case MIPI_DSI_DT_DCS_SHORT_WRITE_0:
    case MIPI_DSI_DT_DCS_SHORT_WRITE_1:
        status = DcsWriteShort(cmd);
        break;
    case MIPI_DSI_DT_DCS_READ_0:
    default:
        DISP_ERROR("Unsupported/Invalid DSI Command type %d\n", cmd.dsi_data_type);
        status = ZX_ERR_INVALID_ARGS;
    }

    if (status != ZX_OK) {
        DISP_ERROR("Something went wrong is sending command\n");
        DumpCmd(cmd);
    }

    return status;
}

zx_status_t DwMipiDsi::Cmd(const uint8_t* tbuf, size_t tlen, uint8_t* rbuf, size_t rlen,
                           bool is_dcs) {
    ZX_DEBUG_ASSERT(initialized_);
    // Create a command packet
    MipiDsiCmd cmd;
    cmd.virt_chn_id = MIPI_DSI_VIRTUAL_CHAN_ID; // TODO(payamm): change name
    cmd.pld_data = tbuf; // tbuf is allowed to be null
    cmd.pld_size = tlen;
    cmd.rsp_data = rbuf; // rbuf is allowed to be null if rlen is 0
    cmd.rsp_size = rlen;
    cmd.flags = 0;
    cmd.dsi_data_type = MIPI_DSI_DT_UNKNOWN;

    switch (tlen) {
        case 0:
            if (rbuf && rlen > 0) {
                cmd.dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_READ_0 :
                                            MIPI_DSI_DT_GEN_SHORT_READ_0;
                cmd.flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
            } else {
                cmd.dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_SHORT_WRITE_0 :
                                            MIPI_DSI_DT_GEN_SHORT_WRITE_0;
            }
            break;
        case 1:
            if (rbuf && rlen > 0) {
                if (is_dcs) {
                    DISP_ERROR("Invalid DCS Read request\n");
                    return ZX_ERR_INVALID_ARGS;
                }
                cmd.dsi_data_type = MIPI_DSI_DT_GEN_SHORT_READ_1;
                cmd.flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
            } else {
                cmd.dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_SHORT_WRITE_1 :
                                            MIPI_DSI_DT_GEN_SHORT_WRITE_1;
            }
            break;
        case 2:
            if (is_dcs) {
                DISP_ERROR("Invalid DCS request\n");
                return ZX_ERR_INVALID_ARGS;
            }
            if (rbuf && rlen > 0) {
                cmd.flags |= MIPI_DSI_CMD_FLAGS_ACK | MIPI_DSI_CMD_FLAGS_SET_MAX;
            } else {
                cmd.dsi_data_type = MIPI_DSI_DT_GEN_SHORT_WRITE_2;
            }
            break;
        default:
            if (rbuf || rlen > 0) {
                DISP_ERROR("Invalid DSI GEN READ Command!\n");
                return ZX_ERR_INVALID_ARGS;
            } else {
                cmd.dsi_data_type = is_dcs ? MIPI_DSI_DT_DCS_LONG_WRITE :
                                            MIPI_DSI_DT_GEN_LONG_WRITE;
            }
            break;
    }

    // packet command has been created.
    return SendCmd(cmd);
}

zx_status_t DwMipiDsi::Init(zx_device_t* parent) {
    if (initialized_) {
        return ZX_OK;
    }
    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &pdev_);
    if (status != ZX_OK) {
        DISP_ERROR("DwMipiDsi: Could not get ZX_PROTOCOL_PLATFORKM_DEV protocol\n");
        return status;
    }

    // Map Mipi Dsi registers
    mmio_buffer_t mmio;
    status = pdev_map_mmio_buffer2(&pdev_, MMIO_MPI_DSI, ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &mmio);
    if (status != ZX_OK) {
        DISP_ERROR("DwMipiDsi: Could not map MIPI DSI mmio\n");
        return status;
    }
    mipi_dsi_mmio_ = fbl::make_unique<ddk::MmioBuffer>(mmio);

    initialized_ = true;
    return status;
}

} // namespace astro_display
