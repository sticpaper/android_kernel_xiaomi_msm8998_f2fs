/*
 * <linux/usb/gadget.h>
 *
 * We call the USB code inside a Linux-based peripheral device a "gadget"
 * driver, except for the hardware-specific bus glue.  One USB host can
 * master many USB gadgets, but the gadgets are only slaved to one host.
 *
 *
 * (C) Copyright 2002-2004 by David Brownell
 * All Rights Reserved.
 *
 * This software is licensed under the GNU GPL version 2.
 */

#ifndef __LINUX_USB_GADGET_H
#define __LINUX_USB_GADGET_H

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/usb/ch9.h>
#include <linux/pm_runtime.h>

struct usb_ep;

enum ep_type {
	EP_TYPE_NORMAL = 0,
	EP_TYPE_GSI,
};

/* Operations codes for GSI enabled EPs */
enum gsi_ep_op {
	GSI_EP_OP_CONFIG = 0,
	GSI_EP_OP_STARTXFER,
	GSI_EP_OP_STORE_DBL_INFO,
	GSI_EP_OP_ENABLE_GSI,
	GSI_EP_OP_UPDATEXFER,
	GSI_EP_OP_RING_IN_DB,
	GSI_EP_OP_ENDXFER,
	GSI_EP_OP_GET_CH_INFO,
	GSI_EP_OP_GET_XFER_IDX,
	GSI_EP_OP_PREPARE_TRBS,
	GSI_EP_OP_FREE_TRBS,
	GSI_EP_OP_SET_CLR_BLOCK_DBL,
	GSI_EP_OP_CHECK_FOR_SUSPEND,
	GSI_EP_OP_DISABLE,
};

/*
 * @buf_base_addr: Base pointer to buffer allocated for each GSI enabled EP.
 *	TRBs point to buffers that are split from this pool. The size of the
 *	buffer is num_bufs times buf_len. num_bufs and buf_len are determined
	based on desired performance and aggregation size.
 * @dma: DMA address corresponding to buf_base_addr.
 * @num_bufs: Number of buffers associated with the GSI enabled EP. This
 *	corresponds to the number of non-zlp TRBs allocated for the EP.
 *	The value is determined based on desired performance for the EP.
 * @buf_len: Size of each individual buffer is determined based on aggregation
 *	negotiated as per the protocol. In case of no aggregation supported by
 *	the protocol, we use default values.
 */
struct usb_gsi_request {
	void *buf_base_addr;
	dma_addr_t dma;
	size_t num_bufs;
	size_t buf_len;
};

/*
 * @last_trb_addr: Address (LSB - based on alignment restrictions) of
 *	last TRB in queue. Used to identify rollover case.
 * @const_buffer_size: TRB buffer size in KB (similar to IPA aggregation
 *	configuration). Must be aligned to Max USB Packet Size.
 *	Should be 1 <= const_buffer_size <= 31.
 * @depcmd_low_addr: Used by GSI hardware to write "Update Transfer" cmd
 * @depcmd_hi_addr: Used to write "Update Transfer" command.
 * @gevntcount_low_addr: GEVNCOUNT low address for GSI hardware to read and
 *	clear processed events.
 * @gevntcount_hi_addr:	GEVNCOUNT high address.
 * @xfer_ring_len: length of transfer ring in bytes (must be integral
 *	multiple of TRB size - 16B for xDCI).
 * @xfer_ring_base_addr: physical base address of transfer ring. Address must
 *	be aligned to xfer_ring_len rounded to power of two.
 * @ch_req: Used to pass request specific info for certain operations on GSI EP
 */
struct gsi_channel_info {
	u16 last_trb_addr;
	u8 const_buffer_size;
	u32 depcmd_low_addr;
	u8 depcmd_hi_addr;
	u32 gevntcount_low_addr;
	u8 gevntcount_hi_addr;
	u16 xfer_ring_len;
	u64 xfer_ring_base_addr;
	struct usb_gsi_request *ch_req;
};

/**
 * struct usb_request - describes one i/o request
 * @buf: Buffer used for data.  Always provide this; some controllers
 *	only use PIO, or don't use DMA for some endpoints.
 * @dma: DMA address corresponding to 'buf'.  If you don't set this
 *	field, and the usb controller needs one, it is responsible
 *	for mapping and unmapping the buffer.
 * @sg: a scatterlist for SG-capable controllers.
 * @num_sgs: number of SG entries
 * @num_mapped_sgs: number of SG entries mapped to DMA (internal)
 * @length: Length of that data
 * @stream_id: The stream id, when USB3.0 bulk streams are being used
 * @no_interrupt: If true, hints that no completion irq is needed.
 *	Helpful sometimes with deep request queues that are handled
 *	directly by DMA controllers.
 * @zero: If true, when writing data, makes the last packet be "short"
 *     by adding a zero length packet as needed;
 * @short_not_ok: When reading data, makes short packets be
 *     treated as errors (queue stops advancing till cleanup).
 * @dma_pre_mapped: Tells the USB core driver whether this request should be
 *	DMA-mapped before it is queued to the USB HW. When set to true, it means
 *	that the request has already been mapped in advance and therefore the
 *	USB core driver does NOT need to do DMA-mapping when the request is
 *	queued to the USB HW.
 * @complete: Function called when request completes, so this request and
 *	its buffer may be re-used.  The function will always be called with
 *	interrupts disabled, and it must not sleep.
 *	Reads terminate with a short packet, or when the buffer fills,
 *	whichever comes first.  When writes terminate, some data bytes
 *	will usually still be in flight (often in a hardware fifo).
 *	Errors (for reads or writes) stop the queue from advancing
 *	until the completion function returns, so that any transfers
 *	invalidated by the error may first be dequeued.
 * @context: For use by the completion callback
 * @list: For use by the gadget driver.
 * @status: Reports completion code, zero or a negative errno.
 *	Normally, faults block the transfer queue from advancing until
 *	the completion callback returns.
 *	Code "-ESHUTDOWN" indicates completion caused by device disconnect,
 *	or when the driver disabled the endpoint.
 * @actual: Reports bytes transferred to/from the buffer.  For reads (OUT
 *	transfers) this may be less than the requested length.  If the
 *	short_not_ok flag is set, short reads are treated as errors
 *	even when status otherwise indicates successful completion.
 *	Note that for writes (IN transfers) some data bytes may still
 *	reside in a device-side FIFO when the request is reported as
 *	complete.
 * @udc_priv: Vendor private data in usage by the UDC.
 *
 * These are allocated/freed through the endpoint they're used with.  The
 * hardware's driver can add extra per-request data to the memory it returns,
 * which often avoids separate memory allocations (potential failures),
 * later when the request is queued.
 *
 * Request flags affect request handling, such as whether a zero length
 * packet is written (the "zero" flag), whether a short read should be
 * treated as an error (blocking request queue advance, the "short_not_ok"
 * flag), or hinting that an interrupt is not required (the "no_interrupt"
 * flag, for use with deep request queues).
 *
 * Bulk endpoints can use any size buffers, and can also be used for interrupt
 * transfers. interrupt-only endpoints can be much less functional.
 *
 * NOTE:  this is analogous to 'struct urb' on the host side, except that
 * it's thinner and promotes more pre-allocation.
 */

struct usb_request {
	void			*buf;
	unsigned		length;
	dma_addr_t		dma;

	struct scatterlist	*sg;
	unsigned		num_sgs;
	unsigned		num_mapped_sgs;

	unsigned		stream_id:16;
	unsigned		no_interrupt:1;
	unsigned		zero:1;
	unsigned		short_not_ok:1;
	unsigned		dma_pre_mapped:1;

	void			(*complete)(struct usb_ep *ep,
					struct usb_request *req);
	void			*context;
	struct list_head	list;

	int			status;
	unsigned		actual;
	unsigned		udc_priv;
};

/*-------------------------------------------------------------------------*/

/* endpoint-specific parts of the api to the usb controller hardware.
 * unlike the urb model, (de)multiplexing layers are not required.
 * (so this api could slash overhead if used on the host side...)
 *
 * note that device side usb controllers commonly differ in how many
 * endpoints they support, as well as their capabilities.
 */
struct usb_ep_ops {
	int (*enable) (struct usb_ep *ep,
		const struct usb_endpoint_descriptor *desc);
	int (*disable) (struct usb_ep *ep);

	struct usb_request *(*alloc_request) (struct usb_ep *ep,
		gfp_t gfp_flags);
	void (*free_request) (struct usb_ep *ep, struct usb_request *req);

	int (*queue) (struct usb_ep *ep, struct usb_request *req,
		gfp_t gfp_flags);
	int (*dequeue) (struct usb_ep *ep, struct usb_request *req);

	int (*set_halt) (struct usb_ep *ep, int value);
	int (*set_wedge) (struct usb_ep *ep);

	int (*fifo_status) (struct usb_ep *ep);
	void (*fifo_flush) (struct usb_ep *ep);
	int (*gsi_ep_op)(struct usb_ep *ep, void *op_data,
		enum gsi_ep_op op);
};

/**
 * struct usb_ep_caps - endpoint capabilities description
 * @type_control:Endpoint supports control type (reserved for ep0).
 * @type_iso:Endpoint supports isochronous transfers.
 * @type_bulk:Endpoint supports bulk transfers.
 * @type_int:Endpoint supports interrupt transfers.
 * @dir_in:Endpoint supports IN direction.
 * @dir_out:Endpoint supports OUT direction.
 */
struct usb_ep_caps {
	unsigned type_control:1;
	unsigned type_iso:1;
	unsigned type_bulk:1;
	unsigned type_int:1;
	unsigned dir_in:1;
	unsigned dir_out:1;
};

#define USB_EP_CAPS_TYPE_CONTROL     0x01
#define USB_EP_CAPS_TYPE_ISO         0x02
#define USB_EP_CAPS_TYPE_BULK        0x04
#define USB_EP_CAPS_TYPE_INT         0x08
#define USB_EP_CAPS_TYPE_ALL \
	(USB_EP_CAPS_TYPE_ISO | USB_EP_CAPS_TYPE_BULK | USB_EP_CAPS_TYPE_INT)
#define USB_EP_CAPS_DIR_IN           0x01
#define USB_EP_CAPS_DIR_OUT          0x02
#define USB_EP_CAPS_DIR_ALL  (USB_EP_CAPS_DIR_IN | USB_EP_CAPS_DIR_OUT)

#define USB_EP_CAPS(_type, _dir) \
	{ \
		.type_control = !!(_type & USB_EP_CAPS_TYPE_CONTROL), \
		.type_iso = !!(_type & USB_EP_CAPS_TYPE_ISO), \
		.type_bulk = !!(_type & USB_EP_CAPS_TYPE_BULK), \
		.type_int = !!(_type & USB_EP_CAPS_TYPE_INT), \
		.dir_in = !!(_dir & USB_EP_CAPS_DIR_IN), \
		.dir_out = !!(_dir & USB_EP_CAPS_DIR_OUT), \
	}

/**
 * struct usb_ep - device side representation of USB endpoint
 * @name:identifier for the endpoint, such as "ep-a" or "ep9in-bulk"
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep_list:the gadget's ep_list holds all of its endpoints
 * @caps:The structure describing types and directions supported by endoint.
 * @maxpacket:The maximum packet size used on this endpoint.  The initial
 *	value can sometimes be reduced (hardware allowing), according to
 *      the endpoint descriptor used to configure the endpoint.
 * @maxpacket_limit:The maximum packet size value which can be handled by this
 *	endpoint. It's set once by UDC driver when endpoint is initialized, and
 *	should not be changed. Should not be confused with maxpacket.
 * @max_streams: The maximum number of streams supported
 *	by this EP (0 - 16, actual number is 2^n)
 * @mult: multiplier, 'mult' value for SS Isoc EPs
 * @maxburst: the maximum number of bursts supported by this EP (for usb3)
 * @driver_data:for use by the gadget driver.
 * @address: used to identify the endpoint when finding descriptor that
 *	matches connection speed
 * @desc: endpoint descriptor.  This pointer is set before the endpoint is
 *	enabled and remains valid until the endpoint is disabled.
 * @comp_desc: In case of SuperSpeed support, this is the endpoint companion
 *	descriptor that is used to configure the endpoint
 * @ep_type: Used to specify type of EP eg. normal vs h/w accelerated.
 * @ep_intr_num: Interrupter number for EP.
 * @endless: In case where endless transfer is being initiated, this is set
 *	to disable usb event interrupt for few events.
 *
 * the bus controller driver lists all the general purpose endpoints in
 * gadget->ep_list.  the control endpoint (gadget->ep0) is not in that list,
 * and is accessed only in response to a driver setup() callback.
 */

struct usb_ep {
	void			*driver_data;

	const char		*name;
	const struct usb_ep_ops	*ops;
	struct list_head	ep_list;
	struct usb_ep_caps	caps;
	bool			claimed;
	bool			enabled;
	unsigned		maxpacket:16;
	unsigned		maxpacket_limit:16;
	unsigned		max_streams:16;
	unsigned		mult:2;
	unsigned		maxburst:5;
	u8			address;
	const struct usb_endpoint_descriptor	*desc;
	const struct usb_ss_ep_comp_descriptor	*comp_desc;
	enum ep_type		ep_type;
	u8			ep_num;
	u8			ep_intr_num;
	bool			endless;
};

/*-------------------------------------------------------------------------*/

/**
 * usb_ep_set_maxpacket_limit - set maximum packet size limit for endpoint
 * @ep:the endpoint being configured
 * @maxpacket_limit:value of maximum packet size limit
 *
 * This function should be used only in UDC drivers to initialize endpoint
 * (usually in probe function).
 */
static inline void usb_ep_set_maxpacket_limit(struct usb_ep *ep,
					      unsigned maxpacket_limit)
{
	ep->maxpacket_limit = maxpacket_limit;
	ep->maxpacket = maxpacket_limit;
}

/**
 * usb_ep_enable - configure endpoint, making it usable
 * @ep:the endpoint being configured.  may not be the endpoint named "ep0".
 *	drivers discover endpoints through the ep_list of a usb_gadget.
 *
 * When configurations are set, or when interface settings change, the driver
 * will enable or disable the relevant endpoints.  while it is enabled, an
 * endpoint may be used for i/o until the driver receives a disconnect() from
 * the host or until the endpoint is disabled.
 *
 * the ep0 implementation (which calls this routine) must ensure that the
 * hardware capabilities of each endpoint match the descriptor provided
 * for it.  for example, an endpoint named "ep2in-bulk" would be usable
 * for interrupt transfers as well as bulk, but it likely couldn't be used
 * for iso transfers or for endpoint 14.  some endpoints are fully
 * configurable, with more generic names like "ep-a".  (remember that for
 * USB, "in" means "towards the USB master".)
 *
 * returns zero, or a negative error code.
 */
static inline int usb_ep_enable(struct usb_ep *ep)
{
	int ret;

	if (ep->enabled)
		return 0;

	/* UDC drivers can't handle endpoints with maxpacket size 0 */
	if (usb_endpoint_maxp(ep->desc) == 0) {
		/*
		 * We should log an error message here, but we can't call
		 * dev_err() because there's no way to find the gadget
		 * given only ep.
		 */
		return -EINVAL;
	}

	ret = ep->ops->enable(ep, ep->desc);
	if (ret)
		return ret;

	ep->enabled = true;

	return 0;
}

/**
 * usb_ep_disable - endpoint is no longer usable
 * @ep:the endpoint being unconfigured.  may not be the endpoint named "ep0".
 *
 * no other task may be using this endpoint when this is called.
 * any pending and uncompleted requests will complete with status
 * indicating disconnect (-ESHUTDOWN) before this call returns.
 * gadget drivers must call usb_ep_enable() again before queueing
 * requests to the endpoint.
 *
 * returns zero, or a negative error code.
 */
static inline int usb_ep_disable(struct usb_ep *ep)
{
	int ret;

	if (!ep->enabled)
		return 0;

	ret = ep->ops->disable(ep);
	if (ret)
		return ret;

	ep->enabled = false;

	return 0;
}

/**
 * usb_ep_alloc_request - allocate a request object to use with this endpoint
 * @ep:the endpoint to be used with with the request
 * @gfp_flags:GFP_* flags to use
 *
 * Request objects must be allocated with this call, since they normally
 * need controller-specific setup and may even need endpoint-specific
 * resources such as allocation of DMA descriptors.
 * Requests may be submitted with usb_ep_queue(), and receive a single
 * completion callback.  Free requests with usb_ep_free_request(), when
 * they are no longer needed.
 *
 * Returns the request, or null if one could not be allocated.
 */
static inline struct usb_request *usb_ep_alloc_request(struct usb_ep *ep,
						       gfp_t gfp_flags)
{
	return ep->ops->alloc_request(ep, gfp_flags);
}

/**
 * usb_ep_free_request - frees a request object
 * @ep:the endpoint associated with the request
 * @req:the request being freed
 *
 * Reverses the effect of usb_ep_alloc_request().
 * Caller guarantees the request is not queued, and that it will
 * no longer be requeued (or otherwise used).
 */
static inline void usb_ep_free_request(struct usb_ep *ep,
				       struct usb_request *req)
{
	ep->ops->free_request(ep, req);
}

/**
 * usb_ep_queue - queues (submits) an I/O request to an endpoint.
 * @ep:the endpoint associated with the request
 * @req:the request being submitted
 * @gfp_flags: GFP_* flags to use in case the lower level driver couldn't
 *	pre-allocate all necessary memory with the request.
 *
 * This tells the device controller to perform the specified request through
 * that endpoint (reading or writing a buffer).  When the request completes,
 * including being canceled by usb_ep_dequeue(), the request's completion
 * routine is called to return the request to the driver.  Any endpoint
 * (except control endpoints like ep0) may have more than one transfer
 * request queued; they complete in FIFO order.  Once a gadget driver
 * submits a request, that request may not be examined or modified until it
 * is given back to that driver through the completion callback.
 *
 * Each request is turned into one or more packets.  The controller driver
 * never merges adjacent requests into the same packet.  OUT transfers
 * will sometimes use data that's already buffered in the hardware.
 * Drivers can rely on the fact that the first byte of the request's buffer
 * always corresponds to the first byte of some USB packet, for both
 * IN and OUT transfers.
 *
 * Bulk endpoints can queue any amount of data; the transfer is packetized
 * automatically.  The last packet will be short if the request doesn't fill it
 * out completely.  Zero length packets (ZLPs) should be avoided in portable
 * protocols since not all usb hardware can successfully handle zero length
 * packets.  (ZLPs may be explicitly written, and may be implicitly written if
 * the request 'zero' flag is set.)  Bulk endpoints may also be used
 * for interrupt transfers; but the reverse is not true, and some endpoints
 * won't support every interrupt transfer.  (Such as 768 byte packets.)
 *
 * Interrupt-only endpoints are less functional than bulk endpoints, for
 * example by not supporting queueing or not handling buffers that are
 * larger than the endpoint's maxpacket size.  They may also treat data
 * toggle differently.
 *
 * Control endpoints ... after getting a setup() callback, the driver queues
 * one response (even if it would be zero length).  That enables the
 * status ack, after transferring data as specified in the response.  Setup
 * functions may return negative error codes to generate protocol stalls.
 * (Note that some USB device controllers disallow protocol stall responses
 * in some cases.)  When control responses are deferred (the response is
 * written after the setup callback returns), then usb_ep_set_halt() may be
 * used on ep0 to trigger protocol stalls.  Depending on the controller,
 * it may not be possible to trigger a status-stage protocol stall when the
 * data stage is over, that is, from within the response's completion
 * routine.
 *
 * For periodic endpoints, like interrupt or isochronous ones, the usb host
 * arranges to poll once per interval, and the gadget driver usually will
 * have queued some data to transfer at that time.
 *
 * Returns zero, or a negative error code.  Endpoints that are not enabled
 * report errors; errors will also be
 * reported when the usb peripheral is disconnected.
 */
static inline int usb_ep_queue(struct usb_ep *ep,
			       struct usb_request *req, gfp_t gfp_flags)
{
	return ep->ops->queue(ep, req, gfp_flags);
}

/**
 * usb_ep_dequeue - dequeues (cancels, unlinks) an I/O request from an endpoint
 * @ep:the endpoint associated with the request
 * @req:the request being canceled
 *
 * If the request is still active on the endpoint, it is dequeued and its
 * completion routine is called (with status -ECONNRESET); else a negative
 * error code is returned. This is guaranteed to happen before the call to
 * usb_ep_dequeue() returns.
 *
 * Note that some hardware can't clear out write fifos (to unlink the request
 * at the head of the queue) except as part of disconnecting from usb. Such
 * restrictions prevent drivers from supporting configuration changes,
 * even to configuration zero (a "chapter 9" requirement).
 */
static inline int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	return ep->ops->dequeue(ep, req);
}

/**
 * usb_ep_set_halt - sets the endpoint halt feature.
 * @ep: the non-isochronous endpoint being stalled
 *
 * Use this to stall an endpoint, perhaps as an error report.
 * Except for control endpoints,
 * the endpoint stays halted (will not stream any data) until the host
 * clears this feature; drivers may need to empty the endpoint's request
 * queue first, to make sure no inappropriate transfers happen.
 *
 * Note that while an endpoint CLEAR_FEATURE will be invisible to the
 * gadget driver, a SET_INTERFACE will not be.  To reset endpoints for the
 * current altsetting, see usb_ep_clear_halt().  When switching altsettings,
 * it's simplest to use usb_ep_enable() or usb_ep_disable() for the endpoints.
 *
 * Returns zero, or a negative error code.  On success, this call sets
 * underlying hardware state that blocks data transfers.
 * Attempts to halt IN endpoints will fail (returning -EAGAIN) if any
 * transfer requests are still queued, or if the controller hardware
 * (usually a FIFO) still holds bytes that the host hasn't collected.
 */
static inline int usb_ep_set_halt(struct usb_ep *ep)
{
	return ep->ops->set_halt(ep, 1);
}

/**
 * usb_ep_clear_halt - clears endpoint halt, and resets toggle
 * @ep:the bulk or interrupt endpoint being reset
 *
 * Use this when responding to the standard usb "set interface" request,
 * for endpoints that aren't reconfigured, after clearing any other state
 * in the endpoint's i/o queue.
 *
 * Returns zero, or a negative error code.  On success, this call clears
 * the underlying hardware state reflecting endpoint halt and data toggle.
 * Note that some hardware can't support this request (like pxa2xx_udc),
 * and accordingly can't correctly implement interface altsettings.
 */
static inline int usb_ep_clear_halt(struct usb_ep *ep)
{
	return ep->ops->set_halt(ep, 0);
}

/**
 * usb_ep_set_wedge - sets the halt feature and ignores clear requests
 * @ep: the endpoint being wedged
 *
 * Use this to stall an endpoint and ignore CLEAR_FEATURE(HALT_ENDPOINT)
 * requests. If the gadget driver clears the halt status, it will
 * automatically unwedge the endpoint.
 *
 * Returns zero on success, else negative errno.
 */
static inline int
usb_ep_set_wedge(struct usb_ep *ep)
{
	if (ep->ops->set_wedge)
		return ep->ops->set_wedge(ep);
	else
		return ep->ops->set_halt(ep, 1);
}

/**
 * usb_ep_fifo_status - returns number of bytes in fifo, or error
 * @ep: the endpoint whose fifo status is being checked.
 *
 * FIFO endpoints may have "unclaimed data" in them in certain cases,
 * such as after aborted transfers.  Hosts may not have collected all
 * the IN data written by the gadget driver (and reported by a request
 * completion).  The gadget driver may not have collected all the data
 * written OUT to it by the host.  Drivers that need precise handling for
 * fault reporting or recovery may need to use this call.
 *
 * This returns the number of such bytes in the fifo, or a negative
 * errno if the endpoint doesn't use a FIFO or doesn't support such
 * precise handling.
 */
static inline int usb_ep_fifo_status(struct usb_ep *ep)
{
	if (ep->ops->fifo_status)
		return ep->ops->fifo_status(ep);
	else
		return -EOPNOTSUPP;
}

/**
 * usb_ep_fifo_flush - flushes contents of a fifo
 * @ep: the endpoint whose fifo is being flushed.
 *
 * This call may be used to flush the "unclaimed data" that may exist in
 * an endpoint fifo after abnormal transaction terminations.  The call
 * must never be used except when endpoint is not being used for any
 * protocol translation.
 */
static inline void usb_ep_fifo_flush(struct usb_ep *ep)
{
	if (ep->ops->fifo_flush)
		ep->ops->fifo_flush(ep);
}

/**
 * usb_gsi_ep_op - performs operation on GSI accelerated EP based on EP op code
 *
 * Operations such as EP configuration, TRB allocation, StartXfer etc.
 * See gsi_ep_op for more details.
 */
static inline int usb_gsi_ep_op(struct usb_ep *ep,
		struct usb_gsi_request *req, enum gsi_ep_op op)
{
	if (ep->ops->gsi_ep_op)
		return ep->ops->gsi_ep_op(ep, req, op);

	return -EOPNOTSUPP;
}
/*-------------------------------------------------------------------------*/

struct usb_dcd_config_params {
	__u8  bU1devExitLat;	/* U1 Device exit Latency */
#define USB_DEFAULT_U1_DEV_EXIT_LAT	0x01	/* Less then 1 microsec */
	__le16 bU2DevExitLat;	/* U2 Device exit Latency */
#define USB_DEFAULT_U2_DEV_EXIT_LAT	0x1F4	/* Less then 500 microsec */
};


struct usb_gadget;
struct usb_gadget_driver;
struct usb_udc;

/* the rest of the api to the controller hardware: device operations,
 * which don't involve endpoints (or i/o).
 */
struct usb_gadget_ops {
	int	(*get_frame)(struct usb_gadget *);
	int	(*wakeup)(struct usb_gadget *);
	int	(*func_wakeup)(struct usb_gadget *, int interface_id);
	int	(*set_selfpowered) (struct usb_gadget *, int is_selfpowered);
	int	(*vbus_session) (struct usb_gadget *, int is_active);
	int	(*vbus_draw) (struct usb_gadget *, unsigned mA);
	int	(*pullup) (struct usb_gadget *, int is_on);
	int	(*restart)(struct usb_gadget *);
	int	(*ioctl)(struct usb_gadget *,
				unsigned code, unsigned long param);
	void	(*get_config_params)(struct usb_dcd_config_params *);
	int	(*udc_start)(struct usb_gadget *,
			struct usb_gadget_driver *);
	int	(*udc_stop)(struct usb_gadget *);
	struct usb_ep *(*match_ep)(struct usb_gadget *,
			struct usb_endpoint_descriptor *,
			struct usb_ss_ep_comp_descriptor *);
};

/**
 * struct usb_gadget - represents a usb slave device
 * @work: (internal use) Workqueue to be used for sysfs_notify()
 * @udc: struct usb_udc pointer for this gadget
 * @ops: Function pointers used to access hardware-specific operations.
 * @ep0: Endpoint zero, used when reading or writing responses to
 *	driver setup() requests
 * @ep_list: List of other endpoints supported by the device.
 * @speed: Speed of current connection to USB host.
 * @max_speed: Maximal speed the UDC can handle.  UDC must support this
 *      and all slower speeds.
 * @state: the state we are now (attached, suspended, configured, etc)
 * @name: Identifies the controller hardware type.  Used in diagnostics
 *	and sometimes configuration.
 * @dev: Driver model state for this abstract device.
 * @out_epnum: last used out ep number
 * @in_epnum: last used in ep number
 * @otg_caps: OTG capabilities of this gadget.
 * @sg_supported: true if we can handle scatter-gather
 * @is_otg: True if the USB device port uses a Mini-AB jack, so that the
 *	gadget driver must provide a USB OTG descriptor.
 * @is_a_peripheral: False unless is_otg, the "A" end of a USB cable
 *	is in the Mini-AB jack, and HNP has been used to switch roles
 *	so that the "A" device currently acts as A-Peripheral, not A-Host.
 * @a_hnp_support: OTG device feature flag, indicating that the A-Host
 *	supports HNP at this port.
 * @a_alt_hnp_support: OTG device feature flag, indicating that the A-Host
 *	only supports HNP on a different root port.
 * @b_hnp_enable: OTG device feature flag, indicating that the A-Host
 *	enabled HNP support.
 * @quirk_ep_out_aligned_size: epout requires buffer size to be aligned to
 *	MaxPacketSize.
 * @quirk_avoids_skb_reserve: udc/platform wants to avoid skb_reserve() in
 *	u_ether.c to improve performance.
 * @is_selfpowered: if the gadget is self-powered.
 * @deactivated: True if gadget is deactivated - in deactivated state it cannot
 *	be connected.
 * @connected: True if gadget is connected.
 *
 * Gadgets have a mostly-portable "gadget driver" implementing device
 * functions, handling all usb configurations and interfaces.  Gadget
 * drivers talk to hardware-specific code indirectly, through ops vectors.
 * That insulates the gadget driver from hardware details, and packages
 * the hardware endpoints through generic i/o queues.  The "usb_gadget"
 * and "usb_ep" interfaces provide that insulation from the hardware.
 *
 * Except for the driver data, all fields in this structure are
 * read-only to the gadget driver.  That driver data is part of the
 * "driver model" infrastructure in 2.6 (and later) kernels, and for
 * earlier systems is grouped in a similar structure that's not known
 * to the rest of the kernel.
 *
 * Values of the three OTG device feature flags are updated before the
 * setup() call corresponding to USB_REQ_SET_CONFIGURATION, and before
 * driver suspend() calls.  They are valid only when is_otg, and when the
 * device is acting as a B-Peripheral (so is_a_peripheral is false).
 */
struct usb_gadget {
	struct work_struct		work;
	struct usb_udc			*udc;
	/* readonly to gadget driver */
	const struct usb_gadget_ops	*ops;
	struct usb_ep			*ep0;
	struct list_head		ep_list;	/* of usb_ep */
	enum usb_device_speed		speed;
	enum usb_device_speed		max_speed;
	enum usb_device_state		state;
	const char			*name;
	struct device			dev;
	unsigned			out_epnum;
	unsigned			in_epnum;
	struct usb_otg_caps		*otg_caps;

	unsigned			sg_supported:1;
	unsigned			is_otg:1;
	unsigned			is_a_peripheral:1;
	unsigned			b_hnp_enable:1;
	unsigned			a_hnp_support:1;
	unsigned			a_alt_hnp_support:1;
	unsigned			quirk_ep_out_aligned_size:1;
	unsigned			quirk_altset_not_supp:1;
	unsigned			quirk_stall_not_supp:1;
	unsigned			quirk_zlp_not_supp:1;
	unsigned			quirk_avoids_skb_reserve:1;
	unsigned			is_selfpowered:1;
	unsigned			deactivated:1;
	unsigned			connected:1;
	bool                            remote_wakeup;
};
#define work_to_gadget(w)	(container_of((w), struct usb_gadget, work))

static inline void set_gadget_data(struct usb_gadget *gadget, void *data)
	{ dev_set_drvdata(&gadget->dev, data); }
static inline void *get_gadget_data(struct usb_gadget *gadget)
	{ return dev_get_drvdata(&gadget->dev); }
static inline struct usb_gadget *dev_to_usb_gadget(struct device *dev)
{
	return container_of(dev, struct usb_gadget, dev);
}

/* iterates the non-control endpoints; 'tmp' is a struct usb_ep pointer */
#define gadget_for_each_ep(tmp, gadget) \
	list_for_each_entry(tmp, &(gadget)->ep_list, ep_list)

/**
 * usb_ep_align - returns @len aligned to ep's maxpacketsize.
 * @ep: the endpoint whose maxpacketsize is used to align @len
 * @len: buffer size's length to align to @ep's maxpacketsize
 *
 * This helper is used to align buffer's size to an ep's maxpacketsize.
 */
static inline size_t usb_ep_align(struct usb_ep *ep, size_t len)
{
	int max_packet_size = (size_t)usb_endpoint_maxp(ep->desc) & 0x7ff;

	return round_up(len, max_packet_size);
}

/**
 * usb_ep_align_maybe - returns @len aligned to ep's maxpacketsize if gadget
 *	requires quirk_ep_out_aligned_size, otherwise returns len.
 * @g: controller to check for quirk
 * @ep: the endpoint whose maxpacketsize is used to align @len
 * @len: buffer size's length to align to @ep's maxpacketsize
 *
 * This helper is used in case it's required for any reason to check and maybe
 * align buffer's size to an ep's maxpacketsize.
 */
static inline size_t
usb_ep_align_maybe(struct usb_gadget *g, struct usb_ep *ep, size_t len)
{
	return g->quirk_ep_out_aligned_size ? usb_ep_align(ep, len) : len;
}

/**
 * gadget_is_altset_supported - return true iff the hardware supports
 *	altsettings
 * @g: controller to check for quirk
 */
static inline int gadget_is_altset_supported(struct usb_gadget *g)
{
	return !g->quirk_altset_not_supp;
}

/**
 * gadget_is_stall_supported - return true iff the hardware supports stalling
 * @g: controller to check for quirk
 */
static inline int gadget_is_stall_supported(struct usb_gadget *g)
{
	return !g->quirk_stall_not_supp;
}

/**
 * gadget_is_zlp_supported - return true iff the hardware supports zlp
 * @g: controller to check for quirk
 */
static inline int gadget_is_zlp_supported(struct usb_gadget *g)
{
	return !g->quirk_zlp_not_supp;
}

/**
 * gadget_avoids_skb_reserve - return true iff the hardware would like to avoid
 *	skb_reserve to improve performance.
 * @g: controller to check for quirk
 */
static inline int gadget_avoids_skb_reserve(struct usb_gadget *g)
{
	return g->quirk_avoids_skb_reserve;
}

/**
 * gadget_is_dualspeed - return true iff the hardware handles high speed
 * @g: controller that might support both high and full speeds
 */
static inline int gadget_is_dualspeed(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_HIGH;
}

/**
 * gadget_is_superspeed() - return true if the hardware handles superspeed
 * @g: controller that might support superspeed
 */
static inline int gadget_is_superspeed(struct usb_gadget *g)
{
	return g->max_speed >= USB_SPEED_SUPER;
}

/**
 * gadget_is_otg - return true iff the hardware is OTG-ready
 * @g: controller that might have a Mini-AB connector
 *
 * This is a runtime test, since kernels with a USB-OTG stack sometimes
 * run on boards which only have a Mini-B (or Mini-A) connector.
 */
static inline int gadget_is_otg(struct usb_gadget *g)
{
#ifdef CONFIG_USB_OTG
	return g->is_otg;
#else
	return 0;
#endif
}

/**
 * usb_gadget_frame_number - returns the current frame number
 * @gadget: controller that reports the frame number
 *
 * Returns the usb frame number, normally eleven bits from a SOF packet,
 * or negative errno if this device doesn't support this capability.
 */
static inline int usb_gadget_frame_number(struct usb_gadget *gadget)
{
	return gadget->ops->get_frame(gadget);
}

/**
 * usb_gadget_wakeup - tries to wake up the host connected to this gadget
 * @gadget: controller used to wake up the host
 *
 * Returns zero on success, else negative error code if the hardware
 * doesn't support such attempts, or its support has not been enabled
 * by the usb host.  Drivers must return device descriptors that report
 * their ability to support this, or hosts won't enable it.
 *
 * This may also try to use SRP to wake the host and start enumeration,
 * even if OTG isn't otherwise in use.  OTG devices may also start
 * remote wakeup even when hosts don't explicitly enable it.
 */
static inline int usb_gadget_wakeup(struct usb_gadget *gadget)
{
	if (!gadget->ops->wakeup)
		return -EOPNOTSUPP;
	return gadget->ops->wakeup(gadget);
}

/**
 * usb_gadget_func_wakeup - send a function remote wakeup up notification
 * to the host connected to this gadget
 * @gadget: controller used to wake up the host
 * @interface_id: the interface which triggered the remote wakeup event
 *
 * Returns zero on success. Otherwise, negative error code is returned.
 */
static inline int usb_gadget_func_wakeup(struct usb_gadget *gadget,
	int interface_id)
{
	if (gadget->speed != USB_SPEED_SUPER)
		return -EOPNOTSUPP;

	if (!gadget->ops->func_wakeup)
		return -EOPNOTSUPP;

	return gadget->ops->func_wakeup(gadget, interface_id);
}

/**
 * usb_gadget_set_selfpowered - sets the device selfpowered feature.
 * @gadget:the device being declared as self-powered
 *
 * this affects the device status reported by the hardware driver
 * to reflect that it now has a local power supply.
 *
 * returns zero on success, else negative errno.
 */
static inline int usb_gadget_set_selfpowered(struct usb_gadget *gadget)
{
	if (!gadget->ops->set_selfpowered)
		return -EOPNOTSUPP;
	return gadget->ops->set_selfpowered(gadget, 1);
}

/**
 * usb_gadget_clear_selfpowered - clear the device selfpowered feature.
 * @gadget:the device being declared as bus-powered
 *
 * this affects the device status reported by the hardware driver.
 * some hardware may not support bus-powered operation, in which
 * case this feature's value can never change.
 *
 * returns zero on success, else negative errno.
 */
static inline int usb_gadget_clear_selfpowered(struct usb_gadget *gadget)
{
	if (!gadget->ops->set_selfpowered)
		return -EOPNOTSUPP;
	return gadget->ops->set_selfpowered(gadget, 0);
}

/**
 * usb_gadget_vbus_connect - Notify controller that VBUS is powered
 * @gadget:The device which now has VBUS power.
 * Context: can sleep
 *
 * This call is used by a driver for an external transceiver (or GPIO)
 * that detects a VBUS power session starting.  Common responses include
 * resuming the controller, activating the D+ (or D-) pullup to let the
 * host detect that a USB device is attached, and starting to draw power
 * (8mA or possibly more, especially after SET_CONFIGURATION).
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_vbus_connect(struct usb_gadget *gadget)
{
	if (!gadget->ops->vbus_session)
		return -EOPNOTSUPP;
	return gadget->ops->vbus_session(gadget, 1);
}

/**
 * usb_gadget_vbus_draw - constrain controller's VBUS power usage
 * @gadget:The device whose VBUS usage is being described
 * @mA:How much current to draw, in milliAmperes.  This should be twice
 *	the value listed in the configuration descriptor bMaxPower field.
 *
 * This call is used by gadget drivers during SET_CONFIGURATION calls,
 * reporting how much power the device may consume.  For example, this
 * could affect how quickly batteries are recharged.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	if (!gadget->ops->vbus_draw)
		return -EOPNOTSUPP;
	return gadget->ops->vbus_draw(gadget, mA);
}

/**
 * usb_gadget_vbus_disconnect - notify controller about VBUS session end
 * @gadget:the device whose VBUS supply is being described
 * Context: can sleep
 *
 * This call is used by a driver for an external transceiver (or GPIO)
 * that detects a VBUS power session ending.  Common responses include
 * reversing everything done in usb_gadget_vbus_connect().
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_vbus_disconnect(struct usb_gadget *gadget)
{
	if (!gadget->ops->vbus_session)
		return -EOPNOTSUPP;
	return gadget->ops->vbus_session(gadget, 0);
}

/**
 * usb_gadget_connect - software-controlled connect to USB host
 * @gadget:the peripheral being connected
 *
 * Enables the D+ (or potentially D-) pullup.  The host will start
 * enumerating this gadget when the pullup is active and a VBUS session
 * is active (the link is powered).  This pullup is always enabled unless
 * usb_gadget_disconnect() has been used to disable it.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_connect(struct usb_gadget *gadget)
{
	int ret;

	if (!gadget->ops->pullup)
		return -EOPNOTSUPP;

	if (gadget->deactivated) {
		/*
		 * If gadget is deactivated we only save new state.
		 * Gadget will be connected automatically after activation.
		 */
		gadget->connected = true;
		return 0;
	}

	ret = gadget->ops->pullup(gadget, 1);
	if (!ret)
		gadget->connected = 1;
	return ret;
}

/**
 * usb_gadget_disconnect - software-controlled disconnect from USB host
 * @gadget:the peripheral being disconnected
 *
 * Disables the D+ (or potentially D-) pullup, which the host may see
 * as a disconnect (when a VBUS session is active).  Not all systems
 * support software pullup controls.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_disconnect(struct usb_gadget *gadget)
{
	int ret;

	if (!gadget->ops->pullup)
		return -EOPNOTSUPP;

	if (gadget->deactivated) {
		/*
		 * If gadget is deactivated we only save new state.
		 * Gadget will stay disconnected after activation.
		 */
		gadget->connected = false;
		return 0;
	}

	ret = gadget->ops->pullup(gadget, 0);
	if (!ret)
		gadget->connected = 0;
	return ret;
}

/**
 * usb_gadget_restart - software-controlled reset of USB peripheral connection
 * @gadget:the peripheral being reset
 *
 * Informs controller driver for Vbus LOW followed by Vbus HIGH notification.
 * This performs full hardware reset and re-initialization.
  */
static inline int usb_gadget_restart(struct usb_gadget *gadget)
{
	if (!gadget->ops->restart)
		return -EOPNOTSUPP;
	return gadget->ops->restart(gadget);
}

/**
 * usb_gadget_deactivate - deactivate function which is not ready to work
 * @gadget: the peripheral being deactivated
 *
 * This routine may be used during the gadget driver bind() call to prevent
 * the peripheral from ever being visible to the USB host, unless later
 * usb_gadget_activate() is called.  For example, user mode components may
 * need to be activated before the system can talk to hosts.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_deactivate(struct usb_gadget *gadget)
{
	int ret;

	if (gadget->deactivated)
		return 0;

	if (gadget->connected) {
		ret = usb_gadget_disconnect(gadget);
		if (ret)
			return ret;
		/*
		 * If gadget was being connected before deactivation, we want
		 * to reconnect it in usb_gadget_activate().
		 */
		gadget->connected = true;
	}
	gadget->deactivated = true;

	return 0;
}

/**
 * usb_gadget_activate - activate function which is not ready to work
 * @gadget: the peripheral being activated
 *
 * This routine activates gadget which was previously deactivated with
 * usb_gadget_deactivate() call. It calls usb_gadget_connect() if needed.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_activate(struct usb_gadget *gadget)
{
	if (!gadget->deactivated)
		return 0;

	gadget->deactivated = false;

	/*
	 * If gadget has been connected before deactivation, or became connected
	 * while it was being deactivated, we call usb_gadget_connect().
	 */
	if (gadget->connected)
		return usb_gadget_connect(gadget);

	return 0;
}

/**
 * usb_gadget_autopm_get - increment PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine should be called by function driver when it wants to use
 * gadget's parent device and needs to guarantee that it is not suspended. In
 * addition, the routine prevents subsequent autosuspends of gadget's parent
 * device. However if the autoresume fails then the counter is re-decremented.
 *
 * This routine can run only in process context.
 */
static inline int usb_gadget_autopm_get(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get_sync(gadget->dev.parent);
	if (status < 0)
		pm_runtime_put_sync(gadget->dev.parent);

	if (status > 0)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_async - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments @gadget parent device PM usage counter and queue an
 * autoresume request if the device is suspended. It does not autoresume device
 * directly (it only queues a request). After a successful call, the device may
 * not yet be resumed.
 *
 * This routine can run in atomic context.
 */
static inline int usb_gadget_autopm_get_async(struct usb_gadget *gadget)
{
	int status = -ENODEV;

	if (!gadget || !gadget->dev.parent)
		return status;

	status = pm_runtime_get(gadget->dev.parent);
	if (status < 0 && status != -EINPROGRESS)
		pm_runtime_put_noidle(gadget->dev.parent);

	if (status > 0 || status == -EINPROGRESS)
		status = 0;
	return status;
}

/**
 * usb_gadget_autopm_get_noresume - increment PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is incremented
 *
 * This routine increments PM-usage count of @gadget parent device but does not
 * carry out an autoresume.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_get_noresume(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_get_noresume(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put - decrement PM-usage counter of usb gadget's parent
 * device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine should be called by function driver when it is finished using
 * @gadget parent device and wants to allow it to autosuspend. It decrements
 * PM-usage counter of @gadget parent device, when the counter reaches 0, a
 * delayed autosuspend request is attempted.
 *
 * This routine can run only in process context.
 */
static inline void usb_gadget_autopm_put(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put_sync(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put_async - decrement PM-usage counter of usb gadget's
 * parent device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine decrements PM-usage counter of @gadget parent device and
 * schedules a delayed autosuspend request if the counter is <= 0.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_put_async(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put(gadget->dev.parent);
}

/**
 * usb_gadget_autopm_put_no_suspend - decrement PM-usage counter of usb gadget
's
 * parent device.
 * @gadget: usb gadget whose parent device counter is decremented.
 *
 * This routine decrements PM-usage counter of @gadget parent device but does
 * not carry out an autosuspend.
 *
 * This routine can run in atomic context.
 */
static inline void usb_gadget_autopm_put_no_suspend(struct usb_gadget *gadget)
{
	if (gadget && gadget->dev.parent)
		pm_runtime_put_noidle(gadget->dev.parent);
}

/*-------------------------------------------------------------------------*/

/**
 * struct usb_gadget_driver - driver for usb 'slave' devices
 * @function: String describing the gadget's function
 * @max_speed: Highest speed the driver handles.
 * @setup: Invoked for ep0 control requests that aren't handled by
 *	the hardware level driver. Most calls must be handled by
 *	the gadget driver, including descriptor and configuration
 *	management.  The 16 bit members of the setup data are in
 *	USB byte order. Called in_interrupt; this may not sleep.  Driver
 *	queues a response to ep0, or returns negative to stall.
 * @disconnect: Invoked after all transfers have been stopped,
 *	when the host is disconnected.  May be called in_interrupt; this
 *	may not sleep.  Some devices can't detect disconnect, so this might
 *	not be called except as part of controller shutdown.
 * @bind: the driver's bind callback
 * @unbind: Invoked when the driver is unbound from a gadget,
 *	usually from rmmod (after a disconnect is reported).
 *	Called in a context that permits sleeping.
 * @suspend: Invoked on USB suspend.  May be called in_interrupt.
 * @resume: Invoked on USB resume.  May be called in_interrupt.
 * @reset: Invoked on USB bus reset. It is mandatory for all gadget drivers
 *	and should be called in_interrupt.
 * @driver: Driver model state for this driver.
 *
 * Devices are disabled till a gadget driver successfully bind()s, which
 * means the driver will handle setup() requests needed to enumerate (and
 * meet "chapter 9" requirements) then do some useful work.
 *
 * If gadget->is_otg is true, the gadget driver must provide an OTG
 * descriptor during enumeration, or else fail the bind() call.  In such
 * cases, no USB traffic may flow until both bind() returns without
 * having called usb_gadget_disconnect(), and the USB host stack has
 * initialized.
 *
 * Drivers use hardware-specific knowledge to configure the usb hardware.
 * endpoint addressing is only one of several hardware characteristics that
 * are in descriptors the ep0 implementation returns from setup() calls.
 *
 * Except for ep0 implementation, most driver code shouldn't need change to
 * run on top of different usb controllers.  It'll use endpoints set up by
 * that ep0 implementation.
 *
 * The usb controller driver handles a few standard usb requests.  Those
 * include set_address, and feature flags for devices, interfaces, and
 * endpoints (the get_status, set_feature, and clear_feature requests).
 *
 * Accordingly, the driver's setup() callback must always implement all
 * get_descriptor requests, returning at least a device descriptor and
 * a configuration descriptor.  Drivers must make sure the endpoint
 * descriptors match any hardware constraints. Some hardware also constrains
 * other descriptors. (The pxa250 allows only configurations 1, 2, or 3).
 *
 * The driver's setup() callback must also implement set_configuration,
 * and should also implement set_interface, get_configuration, and
 * get_interface.  Setting a configuration (or interface) is where
 * endpoints should be activated or (config 0) shut down.
 *
 * (Note that only the default control endpoint is supported.  Neither
 * hosts nor devices generally support control traffic except to ep0.)
 *
 * Most devices will ignore USB suspend/resume operations, and so will
 * not provide those callbacks.  However, some may need to change modes
 * when the host is not longer directing those activities.  For example,
 * local controls (buttons, dials, etc) may need to be re-enabled since
 * the (remote) host can't do that any longer; or an error state might
 * be cleared, to make the device behave identically whether or not
 * power is maintained.
 */
struct usb_gadget_driver {
	char			*function;
	enum usb_device_speed	max_speed;
	int			(*bind)(struct usb_gadget *gadget,
					struct usb_gadget_driver *driver);
	void			(*unbind)(struct usb_gadget *);
	int			(*setup)(struct usb_gadget *,
					const struct usb_ctrlrequest *);
	void			(*disconnect)(struct usb_gadget *);
	void			(*suspend)(struct usb_gadget *);
	void			(*resume)(struct usb_gadget *);
	void			(*reset)(struct usb_gadget *);

	/* FIXME support safe rmmod */
	struct device_driver	driver;
};



/*-------------------------------------------------------------------------*/

/* driver modules register and unregister, as usual.
 * these calls must be made in a context that can sleep.
 *
 * these will usually be implemented directly by the hardware-dependent
 * usb bus interface driver, which will only support a single driver.
 */

/**
 * usb_gadget_probe_driver - probe a gadget driver
 * @driver: the driver being registered
 * Context: can sleep
 *
 * Call this in your gadget driver's module initialization function,
 * to tell the underlying usb controller driver about your driver.
 * The @bind() function will be called to bind it to a gadget before this
 * registration call returns.  It's expected that the @bind() function will
 * be in init sections.
 */
int usb_gadget_probe_driver(struct usb_gadget_driver *driver);

/**
 * usb_gadget_unregister_driver - unregister a gadget driver
 * @driver:the driver being unregistered
 * Context: can sleep
 *
 * Call this in your gadget driver's module cleanup function,
 * to tell the underlying usb controller that your driver is
 * going away.  If the controller is connected to a USB host,
 * it will first disconnect().  The driver is also requested
 * to unbind() and clean up any device state, before this procedure
 * finally returns.  It's expected that the unbind() functions
 * will in in exit sections, so may not be linked in some kernels.
 */
int usb_gadget_unregister_driver(struct usb_gadget_driver *driver);

extern int usb_add_gadget_udc_release(struct device *parent,
		struct usb_gadget *gadget, void (*release)(struct device *dev));
extern int usb_add_gadget_udc(struct device *parent, struct usb_gadget *gadget);
extern void usb_del_gadget_udc(struct usb_gadget *gadget);
extern int usb_udc_attach_driver(const char *name,
		struct usb_gadget_driver *driver);

/*-------------------------------------------------------------------------*/

/* utility to simplify dealing with string descriptors */

/**
 * struct usb_string - wraps a C string and its USB id
 * @id:the (nonzero) ID for this string
 * @s:the string, in UTF-8 encoding
 *
 * If you're using usb_gadget_get_string(), use this to wrap a string
 * together with its ID.
 */
struct usb_string {
	u8			id;
	const char		*s;
};

/**
 * struct usb_gadget_strings - a set of USB strings in a given language
 * @language:identifies the strings' language (0x0409 for en-us)
 * @strings:array of strings with their ids
 *
 * If you're using usb_gadget_get_string(), use this to wrap all the
 * strings for a given language.
 */
struct usb_gadget_strings {
	u16			language;	/* 0x0409 for en-us */
	struct usb_string	*strings;
};

struct usb_gadget_string_container {
	struct list_head        list;
	u8                      *stash[0];
};

/* put descriptor for string with that id into buf (buflen >= 256) */
int usb_gadget_get_string(struct usb_gadget_strings *table, int id, u8 *buf);

/*-------------------------------------------------------------------------*/

/* utility to simplify managing config descriptors */

/* write vector of descriptors into buffer */
int usb_descriptor_fillbuf(void *, unsigned,
		const struct usb_descriptor_header **);

/* build config descriptor from single descriptor vector */
int usb_gadget_config_buf(const struct usb_config_descriptor *config,
	void *buf, unsigned buflen, const struct usb_descriptor_header **desc);

/* copy a NULL-terminated vector of descriptors */
struct usb_descriptor_header **usb_copy_descriptors(
		struct usb_descriptor_header **);

/**
 * usb_free_descriptors - free descriptors returned by usb_copy_descriptors()
 * @v: vector of descriptors
 */
static inline void usb_free_descriptors(struct usb_descriptor_header **v)
{
	kfree(v);
	v = NULL;
}

struct usb_function;
int usb_assign_descriptors(struct usb_function *f,
		struct usb_descriptor_header **fs,
		struct usb_descriptor_header **hs,
		struct usb_descriptor_header **ss);
void usb_free_all_descriptors(struct usb_function *f);

struct usb_descriptor_header *usb_otg_descriptor_alloc(
				struct usb_gadget *gadget);
int usb_otg_descriptor_init(struct usb_gadget *gadget,
		struct usb_descriptor_header *otg_desc);
/*-------------------------------------------------------------------------*/

/**
 * usb_func_ep_queue - queues (submits) an I/O request to a function endpoint.
 * This function is similar to the usb_ep_queue function, but in addition it
 * also checks whether the function is in Super Speed USB Function Suspend
 * state, and if so a Function Wake notification is sent to the host
 * (USB 3.0 spec, section 9.2.5.2).
 * @func: the function which issues the USB I/O request.
 * @ep:the endpoint associated with the request
 * @req:the request being submitted
 * @gfp_flags: GFP_* flags to use in case the lower level driver couldn't
 *	pre-allocate all necessary memory with the request.
 *
 */
int usb_func_ep_queue(struct usb_function *func, struct usb_ep *ep,
				struct usb_request *req, gfp_t gfp_flags);

/*-------------------------------------------------------------------------*/

/* utility to simplify map/unmap of usb_requests to/from DMA */

extern int usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);

extern void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in);

/*-------------------------------------------------------------------------*/

/* utility to set gadget state properly */

extern void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state);

/*-------------------------------------------------------------------------*/

/* utility to tell udc core that the bus reset occurs */
extern void usb_gadget_udc_reset(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver);

/*-------------------------------------------------------------------------*/

/* utility to give requests back to the gadget layer */

extern void usb_gadget_giveback_request(struct usb_ep *ep,
		struct usb_request *req);

/*-------------------------------------------------------------------------*/

/* utility to find endpoint by name */

extern struct usb_ep *gadget_find_ep_by_name(struct usb_gadget *g,
		const char *name);

/*-------------------------------------------------------------------------*/

/* utility to check if endpoint caps match descriptor needs */

extern int usb_gadget_ep_match_desc(struct usb_gadget *gadget,
		struct usb_ep *ep, struct usb_endpoint_descriptor *desc,
		struct usb_ss_ep_comp_descriptor *ep_comp);

/*-------------------------------------------------------------------------*/

/* utility to update vbus status for udc core, it may be scheduled */
extern void usb_udc_vbus_handler(struct usb_gadget *gadget, bool status);

/*-------------------------------------------------------------------------*/

/* utility wrapping a simple endpoint selection policy */

extern struct usb_ep *usb_ep_autoconfig(struct usb_gadget *,
			struct usb_endpoint_descriptor *);


extern struct usb_ep *usb_ep_autoconfig_ss(struct usb_gadget *,
			struct usb_endpoint_descriptor *,
			struct usb_ss_ep_comp_descriptor *);

extern void usb_ep_autoconfig_release(struct usb_ep *);

extern void usb_ep_autoconfig_reset(struct usb_gadget *);
extern struct usb_ep *usb_ep_autoconfig_by_name(struct usb_gadget *,
			struct usb_endpoint_descriptor *,
			const char *ep_name);

#endif /* __LINUX_USB_GADGET_H */
