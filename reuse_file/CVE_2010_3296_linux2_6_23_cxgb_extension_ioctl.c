
static int CVE_2010_3296_linux2_6_23_cxgb_extension_ioctl(struct net_device *dev, void __user *useraddr)
{
	struct port_info *pi = netdev_priv(dev);
	struct adapter *adapter = pi->adapter;
	u32 cmd;
	int ret;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	switch (cmd) {
	case CHELSIO_SET_QSET_PARAMS:{
		int i;
		struct qset_params *q;
		struct ch_qset_params t;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.qset_idx >= SGE_QSETS)
			return -EINVAL;
		if (!in_range(t.intr_lat, 0, M_NEWTIMER) ||
			!in_range(t.cong_thres, 0, 255) ||
			!in_range(t.txq_size[0], MIN_TXQ_ENTRIES,
				MAX_TXQ_ENTRIES) ||
			!in_range(t.txq_size[1], MIN_TXQ_ENTRIES,
				MAX_TXQ_ENTRIES) ||
			!in_range(t.txq_size[2], MIN_CTRL_TXQ_ENTRIES,
				MAX_CTRL_TXQ_ENTRIES) ||
			!in_range(t.fl_size[0], MIN_FL_ENTRIES,
				MAX_RX_BUFFERS)
			|| !in_range(t.fl_size[1], MIN_FL_ENTRIES,
					MAX_RX_JUMBO_BUFFERS)
			|| !in_range(t.rspq_size, MIN_RSPQ_ENTRIES,
					MAX_RSPQ_ENTRIES))
			return -EINVAL;
		if ((adapter->flags & FULL_INIT_DONE) &&
			(t.rspq_size >= 0 || t.fl_size[0] >= 0 ||
			t.fl_size[1] >= 0 || t.txq_size[0] >= 0 ||
			t.txq_size[1] >= 0 || t.txq_size[2] >= 0 ||
			t.polling >= 0 || t.cong_thres >= 0))
			return -EBUSY;

		q = &adapter->params.sge.qset[t.qset_idx];

		if (t.rspq_size >= 0)
			q->rspq_size = t.rspq_size;
		if (t.fl_size[0] >= 0)
			q->fl_size = t.fl_size[0];
		if (t.fl_size[1] >= 0)
			q->jumbo_size = t.fl_size[1];
		if (t.txq_size[0] >= 0)
			q->txq_size[0] = t.txq_size[0];
		if (t.txq_size[1] >= 0)
			q->txq_size[1] = t.txq_size[1];
		if (t.txq_size[2] >= 0)
			q->txq_size[2] = t.txq_size[2];
		if (t.cong_thres >= 0)
			q->cong_thres = t.cong_thres;
		if (t.intr_lat >= 0) {
			struct sge_qset *qs =
				&adapter->sge.qs[t.qset_idx];

			q->coalesce_usecs = t.intr_lat;
			t3_update_qset_coalesce(qs, q);
		}
		if (t.polling >= 0) {
			if (adapter->flags & USING_MSIX)
				q->polling = t.polling;
			else {
				/* No polling with INTx for T3A */
				if (adapter->params.rev == 0 &&
					!(adapter->flags & USING_MSI))
					t.polling = 0;

				for (i = 0; i < SGE_QSETS; i++) {
					q = &adapter->params.sge.
						qset[i];
					q->polling = t.polling;
				}
			}
		}
		break;
	}
	case CHELSIO_GET_QSET_PARAMS:{
		struct qset_params *q;
		struct ch_qset_params t;

		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if (t.qset_idx >= SGE_QSETS)
			return -EINVAL;

		q = &adapter->params.sge.qset[t.qset_idx];
		t.rspq_size = q->rspq_size;
		t.txq_size[0] = q->txq_size[0];
		t.txq_size[1] = q->txq_size[1];
		t.txq_size[2] = q->txq_size[2];
		t.fl_size[0] = q->fl_size;
		t.fl_size[1] = q->jumbo_size;
		t.polling = q->polling;
		t.intr_lat = q->coalesce_usecs;
		t.cong_thres = q->cong_thres;

		if (copy_to_user(useraddr, &t, sizeof(t)))
			return -EFAULT;
		break;
	}
	case CHELSIO_SET_QSET_NUM:{
		struct ch_reg edata;
		struct port_info *pi = netdev_priv(dev);
		unsigned int i, first_qset = 0, other_qsets = 0;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (adapter->flags & FULL_INIT_DONE)
			return -EBUSY;
		if (copy_from_user(&edata, useraddr, sizeof(edata)))
			return -EFAULT;
		if (edata.val < 1 ||
			(edata.val > 1 && !(adapter->flags & USING_MSIX)))
			return -EINVAL;

		for_each_port(adapter, i)
			if (adapter->port[i] && adapter->port[i] != dev)
				other_qsets += adap2pinfo(adapter, i)->nqsets;

		if (edata.val + other_qsets > SGE_QSETS)
			return -EINVAL;

		pi->nqsets = edata.val;

		for_each_port(adapter, i)
			if (adapter->port[i]) {
				pi = adap2pinfo(adapter, i);
				pi->first_qset = first_qset;
				first_qset += pi->nqsets;
			}
		break;
	}
	case CHELSIO_GET_QSET_NUM:{
		struct ch_reg edata;
		struct port_info *pi = netdev_priv(dev);

		edata.cmd = CHELSIO_GET_QSET_NUM;
		edata.val = pi->nqsets;
		if (copy_to_user(useraddr, &edata, sizeof(edata)))
			return -EFAULT;
		break;
	}
	case CHELSIO_LOAD_FW:{
		u8 *fw_data;
		struct ch_mem_range t;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;

		fw_data = kmalloc(t.len, GFP_KERNEL);
		if (!fw_data)
			return -ENOMEM;

		if (copy_from_user
			(fw_data, useraddr + sizeof(t), t.len)) {
			kfree(fw_data);
			return -EFAULT;
		}

		ret = t3_load_fw(adapter, fw_data, t.len);
		kfree(fw_data);
		if (ret)
			return ret;
		break;
	}
	case CHELSIO_SETMTUTAB:{
		struct ch_mtus m;
		int i;

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (offload_running(adapter))
			return -EBUSY;
		if (copy_from_user(&m, useraddr, sizeof(m)))
			return -EFAULT;
		if (m.nmtus != NMTUS)
			return -EINVAL;
		if (m.mtus[0] < 81)	/* accommodate SACK */
			return -EINVAL;

		/* MTUs must be in ascending order */
		for (i = 1; i < NMTUS; ++i)
			if (m.mtus[i] < m.mtus[i - 1])
				return -EINVAL;

		memcpy(adapter->params.mtus, m.mtus,
			sizeof(adapter->params.mtus));
		break;
	}
	case CHELSIO_GET_PM:{
		struct tp_params *p = &adapter->params.tp;
		struct ch_pm m = {.cmd = CHELSIO_GET_PM };

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		m.tx_pg_sz = p->tx_pg_size;
		m.tx_num_pg = p->tx_num_pgs;
		m.rx_pg_sz = p->rx_pg_size;
		m.rx_num_pg = p->rx_num_pgs;
		m.pm_total = p->pmtx_size + p->chan_rx_size * p->nchan;
		if (copy_to_user(useraddr, &m, sizeof(m)))
			return -EFAULT;
		break;
	}
	case CHELSIO_SET_PM:{
		struct ch_pm m;
		struct tp_params *p = &adapter->params.tp;

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (adapter->flags & FULL_INIT_DONE)
			return -EBUSY;
		if (copy_from_user(&m, useraddr, sizeof(m)))
			return -EFAULT;
		if (!is_power_of_2(m.rx_pg_sz) ||
			!is_power_of_2(m.tx_pg_sz))
			return -EINVAL;	/* not power of 2 */
		if (!(m.rx_pg_sz & 0x14000))
			return -EINVAL;	/* not 16KB or 64KB */
		if (!(m.tx_pg_sz & 0x1554000))
			return -EINVAL;
		if (m.tx_num_pg == -1)
			m.tx_num_pg = p->tx_num_pgs;
		if (m.rx_num_pg == -1)
			m.rx_num_pg = p->rx_num_pgs;
		if (m.tx_num_pg % 24 || m.rx_num_pg % 24)
			return -EINVAL;
		if (m.rx_num_pg * m.rx_pg_sz > p->chan_rx_size ||
			m.tx_num_pg * m.tx_pg_sz > p->chan_tx_size)
			return -EINVAL;
		p->rx_pg_size = m.rx_pg_sz;
		p->tx_pg_size = m.tx_pg_sz;
		p->rx_num_pgs = m.rx_num_pg;
		p->tx_num_pgs = m.tx_num_pg;
		break;
	}
	case CHELSIO_GET_MEM:{
		struct ch_mem_range t;
		struct mc7 *mem;
		u64 buf[32];

		if (!is_offload(adapter))
			return -EOPNOTSUPP;
		if (!(adapter->flags & FULL_INIT_DONE))
			return -EIO;	/* need the memory controllers */
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;
		if ((t.addr & 7) || (t.len & 7))
			return -EINVAL;
		if (t.mem_id == MEM_CM)
			mem = &adapter->cm;
		else if (t.mem_id == MEM_PMRX)
			mem = &adapter->pmrx;
		else if (t.mem_id == MEM_PMTX)
			mem = &adapter->pmtx;
		else
			return -EINVAL;

		/*
		 * Version scheme:
		 * bits 0..9: chip version
		 * bits 10..15: chip revision
		 */
		t.version = 3 | (adapter->params.rev << 10);
		if (copy_to_user(useraddr, &t, sizeof(t)))
			return -EFAULT;

		/*
		 * Read 256 bytes at a time as len can be large and we don't
		 * want to use huge intermediate buffers.
		 */
		useraddr += sizeof(t);	/* advance to start of buffer */
		while (t.len) {
			unsigned int chunk =
				min_t(unsigned int, t.len, sizeof(buf));

			ret =
				t3_mc7_bd_read(mem, t.addr / 8, chunk / 8,
						buf);
			if (ret)
				return ret;
			if (copy_to_user(useraddr, buf, chunk))
				return -EFAULT;
			useraddr += chunk;
			t.addr += chunk;
			t.len -= chunk;
		}
		break;
	}
	case CHELSIO_SET_TRACE_FILTER:{
		struct ch_trace t;
		const struct trace_params *tp;

		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		if (!offload_running(adapter))
			return -EAGAIN;
		if (copy_from_user(&t, useraddr, sizeof(t)))
			return -EFAULT;

		tp = (const struct trace_params *)&t.sip;
		if (t.config_tx)
			t3_config_trace_filter(adapter, tp, 0,
						t.invert_match,
						t.trace_tx);
		if (t.config_rx)
			t3_config_trace_filter(adapter, tp, 1,
						t.invert_match,
						t.trace_rx);
		break;
	}
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}