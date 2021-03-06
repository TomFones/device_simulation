/********1*********2*********3*********4*********5**********6*********7*********/
/*                                                                             */
/*  PRODUCT      : MAD Device Simulation Framework                             */
/*  COPYRIGHT    : (c) 2021 HTF Consulting                                     */
/*                                                                             */
/* This source code is provided by Dual/GPL license to the Linux open source   */ 
/* community                                                                   */
/*                                                                             */ 
/*******************************************************************************/
/*                                                                             */
/*  Exe files   : maddevc.ko, maddevb.ko                                       */ 
/*                                                                             */
/*  Module NAME : maddrvrdefs.c                                                */
/*                                                                             */
/*  DESCRIPTION : Function prototypes & definitions for the MAD device drivers */
/*                                                                             */
/*  MODULE_AUTHOR("HTF Consulting");                                           */
/*  MODULE_LICENSE("Dual/GPL");                                                */
/*                                                                             */
/* The source code in this file can be freely used, adapted, and redistributed */
/* in source or binary form, so long as an acknowledgment appears in derived   */
/* source files.  The citation should state that the source code comes from a  */
/* a set of source files developed by HTF Consulting                           */
/* http://www.htfconsulting.com                                                */
/*                                                                             */
/* No warranty is attached.                                                    */
/* HTF Consulting assumes no responsibility for errors or fitness of use       */
/*                                                                             */
/*                                                                             */
/* $Id: maddrvrdefs.c, v 1.0 2021/01/01 00:00:00 htf $                         */
/*                                                                             */
/*******************************************************************************/

#include <asm/io.h>

static struct driver_private maddrvr_priv_data =
{
    .driver = NULL,
};

static struct pci_driver maddev_driver =
{
	.driver = {.name = maddrvrname, .owner = THIS_MODULE, .p = &maddrvr_priv_data,},
    //
	.id_table = pci_ids,
	.probe    = maddev_probe,
    .suspend  = NULL,
    .resume   = NULL,
	.remove   = maddev_remove,
    .shutdown = maddev_shutdown,
};

#if 0
static struct klist maddev_klist;
static struct device_private maddev_priv_data = 
{
    .knode_driver = {.n_klist = &maddev_klist,},
    .dead = 0,
};
#endif

//Function definitions - only to appear in the main module of multiple device drivers
static int maddev_probe(struct pci_dev *pcidev, const struct pci_device_id *ids)
{
    int rc = 0;
    U32 devnum;
    PMADDEVOBJ pmaddevobj = NULL;

    ASSERT((int)(pcidev != NULL));
    devnum = (U32)pcidev->slot;
    pmaddevobj = &mad_dev_objects[devnum];

	PINFO("maddev_probe... slotnum=%d pcidev=%px vendor=x%X pci_devid=x%X\n",
          (int)devnum, pcidev, pcidev->vendor, pcidev->device);

    if ((pcidev->vendor != MAD_PCI_VENDOR_ID) || 
        (pcidev->device < MAD_PCI_CHAR_INT_DEVICE_ID) || 
        (pcidev->device > MAD_PCI_CHAR_MSI_DEVICE_ID)) 
        //
        {return -EINVAL;}

    pmaddevobj->devnum = devnum;
    pmaddevobj->pPciDev = pcidev;

    // Build the device object... 
    rc = maddev_setup_device(pmaddevobj, &pmaddevobj->pPciDev, true);

	return rc;
}
//
static void maddev_shutdown(struct pci_dev *pcidev)
{
    ASSERT((int)(pcidev != NULL));

   	PINFO("maddev_shutdown... pcidev=%px\n", pcidev);

    //What else to do other than remove ?
    maddev_remove(pcidev);

    return;
}

static void maddev_remove(struct pci_dev *pcidev)
{
    U32 devnum; 
    struct mad_dev_obj *pmaddevobj; 
    ASSERT((int)(pcidev != NULL));

    devnum = (U32)pcidev->slot;
    pmaddevobj = &mad_dev_objects[devnum];

	PINFO("maddev_remove... devnum=%d pmaddevobj=%px pcidev=%px\n",
          (int)devnum, pmaddevobj, pcidev);

    if (pmaddevobj != NULL)
        {maddev_remove_device(pmaddevobj);}
}

static struct kset mad_kset;
//
static char MadKsetNm[] = "madkset";
//
static U8  bMadKset = 0;
//
static struct kobj_type mad_ktype = 
{
    .release = NULL,
    .sysfs_ops = NULL,
    .default_attrs = NULL,
};

//This function sets up one pci device object
static int 
maddev_setup_device(PMADDEVOBJ pmaddevobj, struct pci_dev** ppPciDvTmp, U8 bHPL)
{
    U32 i = pmaddevobj->devnum;
    dev_t devno = MKDEV(maddev_major, i);
    //
    struct pci_dev* pPciDevTmp;
    struct pci_dev* ppcidev;
    int rc = 0;
    U8  bMSI;
    int wrc = 0;
    U32 flags1;

	PINFO("maddev_setup_device... dev#=%d pmaddevobj=%px bHPL=%d\n",
		  (int)i, pmaddevobj, bHPL);

    mutex_init(&pmaddevobj->devmutex);
    mutex_lock(&pmaddevobj->devmutex);

    spin_lock_init(&pmaddevobj->devlock);
    //Sanity check
    maddev_acquire_lock_disable_ints(&pmaddevobj->devlock, flags1);
    maddev_enable_ints_release_lock(&pmaddevobj->devlock, flags1);

    //Get the PCI device struct if this is not a discover - not a plugin
    if (!bHPL)
        {
        *ppPciDvTmp = 
        pci_get_device(MAD_PCI_VENDOR_ID, MAD_PCI_CHAR_DEVICE_ID, *ppPciDvTmp);
        if ((*ppPciDvTmp == NULL) || (IS_ERR(*ppPciDvTmp)))
            {
            mutex_unlock(&pmaddevobj->devmutex);
            PERR("maddev_setup_device... dev#=%d pci_get_device rc=%d\n",
                 (int)i, PTR_ERR(*ppPciDvTmp));
            return (int)PTR_ERR(*ppPciDvTmp);
            }
        }
    //
    pPciDevTmp = *ppPciDvTmp;

    //Assign the device name for this device before acquiring resources
	MadDevNames[i][MADDEVOBJNUMDX] = MadDevNumStr[i];

    #ifdef _MAD_SIMULATION_MODE_
    //Exchange parameters w/ the simulator
    rc = maddev_xchange_sim_parms(pPciDevTmp, pmaddevobj);
    if (rc != 0)
		{
        mutex_unlock(&pmaddevobj->devmutex);
        PERR("maddev_setup_device:mad_xchange_sim_parms... dev#=%d rc=%d\n",
             (int)i, rc);
		return rc;
		}
    #endif

    bMSI = pPciDevTmp->msi_cap;
    rc = maddev_claim_pci_resrcs(pPciDevTmp, pmaddevobj,
                                 MadDevNames[i], (1 + bMSI));
    if (rc != 0)
		{
        mutex_unlock(&pmaddevobj->devmutex);
        PERR("maddev_setup_device:maddev_claim_pci_resrcs... dev#=%d rc=%d\n",
		     (int)i, rc);
		return rc;
		}

    //If we discovered the device at startup - not hotplugged
    if (!bHPL) 
        {pmaddevobj->pPciDev = pPciDevTmp;}

    ppcidev = pmaddevobj->pPciDev;
    ppcidev->driver = &maddev_driver;
	maddev_init_io_parms(pmaddevobj, i);

    //Create a device node - the equivalent to mknod in BASH
    pmaddevobj->pdevnode = device_create(mad_class, NULL, /* no parent device */ 
		                                 devno, NULL, /* no additional data */
                                         MadDevNames[i]);
    if (IS_ERR(pmaddevobj->pdevnode)) 
        {
        rc = PTR_ERR(pmaddevobj->pdevnode);
        pmaddevobj->pdevnode = NULL;
		PWARN("maddev_setup_device:device_create... dev#=%d rc=%d\n",
              (int)i, rc);
        //device_create failure may not be fatal
        }

    //Configure & register the generic device w/in pci_dev for the sysfs tree
    ppcidev->dev.init_name = MadDevNames[i];
    ppcidev->dev.driver    = (struct device_driver*)&maddev_driver;
    ppcidev->dev.bus       = NULL;

    rc = register_device(&ppcidev->dev);
    pmaddevobj->bDevRegstrd = (rc == 0) ? true : false;
	if (rc != 0)
		{
        maddev_release_pci_resrcs(pmaddevobj);
        mutex_unlock(&pmaddevobj->devmutex);
        PERR("maddev_setup_device:device_register... dev#=%d rc=%d\n", (int)i);
        return rc;
        }

    #ifndef _MAD_SIMULATION_MODE_
    rc = dma_set_mask(ppcidev, DMA_BIT_MASK(64));
    if (rc != 0)
        {
        maddev_release_pci_resrcs(pmaddevobj);
        mutex_unlock(&pmaddevobj->devmutex);
        PERR("maddev_setup_device:dma_set_mask... dev#=%d rc=%d\n", (int)i, rc);
        return -ENOTSUP;
        }
    #endif
    pmaddevobj->pdevnode = &pmaddevobj->pPciDev->dev;

    #ifdef _CDEV_
	rc = maddev_setup_cdev(pmaddevobj, i);
    #endif

    #ifdef _BLOCKIO_
    rc = maddevb_create_device(pmaddevobj);
    #endif

    if (rc != 0)
		{
        maddev_release_pci_resrcs(pmaddevobj);
        mutex_unlock(&pmaddevobj->devmutex);
        PERR("maddev_setup_device:maddev_create_device... dev#=%d rc=%d\n",
			 (int)i, rc);
        return rc;
		}

    pmaddevobj->bReady = true;
    mutex_unlock(&pmaddevobj->devmutex);

    PINFO("maddev_setup_device... dev=%d pdevobj=%px devVA=%px devPA=x%llX exit :)\n",
          (int)i, pmaddevobj, 
          pmaddevobj->pDevBase, pmaddevobj->MadDevPA);

    //Release our quantum - let asynchronous threads run
  	schedule(); 

    return rc;
}

//This function tears down one device object
static void maddev_remove_device(PMADDEVOBJ pmaddevobj)
{
    int rc;

	PINFO("maddev_remove_device... dev#=%d pmaddevobj=%px\n",
		  (int)pmaddevobj->devnum, pmaddevobj);

    pmaddevobj->bReady = false;

    #ifdef _CDEV_
	cdev_del(&pmaddevobj->cdev_str);
    #endif

    #ifdef _BLOCKIO_
    maddevb_delete_device(pmaddevobj->maddevblk_dev->pmaddevb);
    #endif

    if (pmaddevobj->bDevRegstrd)
        {
        unregister_device(&pmaddevobj->pPciDev->dev);
        pmaddevobj->bDevRegstrd = false;
        }
    else
        {PWARN("maddev_remove_device... dev#=%d device not registered! (%px)\n",
              (int)pmaddevobj->devnum, pmaddevobj->pPciDev->dev);}

    #if 0
    maddev_kobject_unregister(&pmaddevobj->pPciDev->dev.kobj);
    #endif
    if (pmaddevobj->pdevnode != NULL)
        {device_destroy(mad_class, MKDEV(maddev_major, pmaddevobj->devnum));}
    rc = maddev_release_pci_resrcs(pmaddevobj);

    PDEBUG("maddev_remove_device... dev#=%d exit :)\n",(int)pmaddevobj->devnum);
}

/* The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized 
 */ 
static void maddev_cleanup_module(void)
{
	U32 i;
    #ifdef _CDEV_
	dev_t devno = MKDEV(maddev_major, maddev_minor);
    #endif
    PMADDEVOBJ pmaddevobj = NULL;

	PINFO("maddev_cleanup_module... mjr=%d mnr=%d\n",
          maddev_major, maddev_minor);

	/* Release our char dev entries */
	if (mad_dev_objects != NULL)
	    {
		for (i = 1; i <= maddev_max_devs; i++)
		    {
            pmaddevobj = 
            (PMADDEVOBJ)((u8*)mad_dev_objects + (PAGE_SIZE * i));

            if (pmaddevobj->bReady == false)
                {
				PWARN("maddev_cleanup... i=%d dev#=%d pmadobj=%px device not active!\n",
                      (int)i, (int)pmaddevobj->devnum, pmaddevobj);
				continue;
				}

            ASSERT((int)(i == pmaddevobj->devnum));
            maddev_remove_device(pmaddevobj);
		    }

		kfree(mad_dev_objects);
	    }

#ifdef MADDEVOBJ_DEBUG /* use proc only if debugging */
	maddev_remove_proc();
#endif

    if (mad_class != NULL)
        {class_destroy(mad_class);}

	pci_unregister_driver(&maddev_driver);

	/* cleanup_module is never called if registering failed */
    #ifdef _CDEV_
	unregister_chrdev_region(devno, maddev_nbr_devs);
    #endif

    #ifdef _BLOCKIO_
    unregister_blkdev(maddev_major, MAD_MAJOR_DEVICE_NAME);
    #endif

   	PINFO("maddev_cleanup_module... exit :)\n");
}   
    
int maddev_claim_pci_resrcs(struct pci_dev* pPciDev, PMADDEVOBJ pmaddevobj,
                            char* DevName, U32 NumIrqs)
{
    static int irqflags = IRQF_TRIGGER_HIGH | IRQF_TRIGGER_LOW;
    //
    register U32 j = 0;
    //
    U8 bMSI = pPciDev->msi_cap;
    int rc = 0;
    phys_addr_t BaseAddr;
    U64 Bar0end;
    U32 MapLen;
    U32 ResFlags;
    U64 PciCnfgU64 = 0;
    U32 PciCnfgLo = 0;
    U32 PciCnfgHi = 0;
    U8  PciCnfgU8;

    // Determine the device phys addr from Pci-Core *AND* PCI config space
    BaseAddr = pci_resource_start(pPciDev, 0);
    Bar0end  = pci_resource_end(pPciDev, 0);
    MapLen   = (U32)Bar0end - BaseAddr + 1;
    ResFlags = pci_resource_flags(pPciDev, 0);

    //Optional integrity checks - verfiying pci_resource... functions
    ASSERT((int)(MapLen >= MAD_SIZEOF_REGISTER_BLOCK));
    ASSERT((int)(MapLen == pci_resource_len(pPciDev, 0)));
    ASSERT((int)((ResFlags & IORESOURCE_MEM) != 0));

    rc = pci_read_config_dword(pPciDev, PCI_BASE_ADDRESS_0, &PciCnfgLo);
    rc = pci_read_config_dword(pPciDev, (PCI_BASE_ADDRESS_0+4), &PciCnfgHi);
    if (rc != 0)
        {
	    PERR("maddev_claim_pci_resrcs:pci_read_config_dword... dev#=%d rc=%d\n",
             (int)pmaddevobj->devnum, rc);
        return rc;
        }

    PciCnfgU64 = (U64)(PciCnfgHi << 32);
    PciCnfgU64 += PciCnfgLo;
    PDEBUG("pci_read_config_dword... BaseAddr=x%llX PciCnfg_Hi:Lo,64=x%X:%X x%llX\n",
           BaseAddr, PciCnfgHi, PciCnfgLo, PciCnfgU64);

    //Optional integrity check - verify one retrieve of BAR_0 vs another
    //ASSERT((int)(BaseAddr == PciCnfgU64));
    //ASSERT((int)(((U32)(BaseAddr >> 32) == PciCnfgHi)));
    //ASSERT((int)((U32)(BaseAddr & 0x0FFFFFFFF) == PciCnfgLo));

    rc = pci_request_region(pPciDev, 0, DevName);
    if (rc != 0)
        {
	    PERR("maddev_claim_pci_resrcs:pci_request_region... dev=%d rc=%d\n",
             (int)pmaddevobj->devnum, rc);
        return rc;
        }

    //Get a kernel virt addr for the device
    pmaddevobj->MadDevPA = BaseAddr;
    pmaddevobj->pDevBase = phys_to_virt(pmaddevobj->MadDevPA);

    PINFO("maddev_claim_pci_resrcs... dev#=%d PA=x%llX kva=%px\n",
		  (int)pmaddevobj->devnum, pmaddevobj->MadDevPA, pmaddevobj->pDevBase);
	if (pmaddevobj->pDevBase == NULL)
        {
        pci_release_region(pPciDev, 0);
        return -ENOMEM;
        }

    pmaddevobj->pDevBase->Devnum = pmaddevobj->devnum;

    rc = pci_read_config_byte(pPciDev, PCI_INTERRUPT_LINE, &PciCnfgU8);
    if (rc != 0)
        {
	    PERR("maddev_claim_pci_resrcs:pci_read_config_dword... dev=%d rc=%d\n",
             (int)pmaddevobj->devnum, rc);
        return rc;
        }

    //Optional integrity check - verfiying one retrieve of Irq vs another
    ASSERT((int)((int)PciCnfgU8 == pPciDev->irq));

    if (bMSI)
        {
        rc = pci_enable_msi_block(pPciDev, NumIrqs);
        if (rc != 0)
	        {
	        PERR("maddev_claim_pci_resrcs:pci_enable_msi_block... dev=%d rc=%d\n",
		         (int)pmaddevobj->devnum, rc);

            pci_release_region(pPciDev, 0);
	        return rc;
	        }
        }

    // Request the irq# indicated in the pci device object and
    // establish the Int Srvc Routine for the irq#
    #ifdef _BLOCKIO_
    rc = request_irq(pPciDev->irq, maddevb_isr, irqflags, DevName, pmaddevobj);
    #endif
    #ifdef _CDEV_
    rc = request_irq(pPciDev->irq, maddevc_isr, irqflags, DevName, pmaddevobj);
    #endif

    //If the device is MSI-capable do it N more times
    if (bMSI)
        {
        for (j=1; j < NumIrqs; j++)
            {
            if (rc != 0)
                {break;} //Any request_irq failure is fatal

            #ifdef _BLOCKIO_
            rc = request_irq((pPciDev->irq + j), maddevb_isr,
                             irqflags, DevName, pmaddevobj);
            #endif

            #ifdef _CDEV_
            rc = request_irq((pPciDev->irq + j), maddevc_isr,
                             irqflags, DevName, pmaddevobj);
            #endif
            }
        }

    if (rc != 0)
        {
        PERR("maddev_claim_pci_resrcs:request_irq(%d)... dev=%d rc=%d\n",
             (int)(pPciDev->irq+j), (int)pmaddevobj->devnum, rc);

        pci_release_region(pPciDev, 0);
        return rc;
        }

    pmaddevobj->irq = pPciDev->irq;

    rc = pci_enable_device(pPciDev);
	if (rc != 0)
		{
		PERR("maddev_claim_pci_resrcs:pci_enable_device... dev=%d rc=%d\n",
             (int)pmaddevobj->devnum, rc);
        maddev_release_pci_resrcs(pmaddevobj);
        }

    PDEBUG("maddev_claim_pci_resrcs... dev#=%d rc=%d\n",
           (int)pmaddevobj->devnum, rc);

    return rc;
}

// This function releases PCI-related resources
int maddev_release_pci_resrcs(PMADDEVOBJ pmaddevobj)
{
    register U32 j = 0;
    int rc = 0;

    PINFO("maddev_release_pci_resrcs... dev#=%d\n", (int)pmaddevobj->devnum);

    rc = free_irq(pmaddevobj->irq, pmaddevobj);
    if (pmaddevobj->pPciDev->msi_cap != 0)
        {
        for (j=1; j <= 7; j++)
            {rc = free_irq((pmaddevobj->pPciDev->irq+j), pmaddevobj);}

        pci_disable_msi(pmaddevobj->pPciDev);
        }

    rc = pci_disable_device(pmaddevobj->pPciDev);
    pci_release_region(pmaddevobj->pPciDev, 0);

    return rc;
}

ssize_t maddev_xfer_dma_page(PMADDEVOBJ pmaddevobj, struct page* pPage,
                             sector_t sector, bool bWr)
{
    static U32 DXBC = PAGE_SIZE;
    //
    dma_addr_t HostPA = 
               maddev_page_to_dma(pmaddevobj->pdevnode, pPage, PAGE_SIZE, bWr);
    U32    DevLoclAddr = (sector * MAD_SECTOR_SIZE);
    U32    IntEnable = 
           (bWr) ? (MAD_INT_STATUS_ALERT_BIT | MAD_INT_DMA_OUTPUT_BIT) :
                   (MAD_INT_STATUS_ALERT_BIT | MAD_INT_DMA_INPUT_BIT);
    U32    DmaCntl   = MAD_DMA_CNTL_INIT;
    U32    CntlReg   = 0; 
    long   iostat;
    size_t iocount = 0;
    u32 flags1 = 0; 
    U32 flags2 = 0;

    if (bWr)
        {DmaCntl |= MAD_DMA_CNTL_H2D;} //Write == Host2Disk

    PINFO("maddev_xfer_dma_page... dev#=%d PA=x%llX sector=%ld wr=%d\n",
          (int)pmaddevobj->devnum, HostPA, sector, bWr);

    if (dma_mapping_error(pmaddevobj->pdevnode, HostPA))
        {
        PERR("maddev_xfer_dma_page... dev#=%d returning -ENOMEM\n",
             (int)pmaddevobj->devnum);
        ASSERT((int)false);
        return -EADDRNOTAVAIL;
        }

    pmaddevobj->ioctl_f = eIoPending;

    maddev_acquire_lock_disable_ints(&pmaddevobj->devlock, flags1);
    //
    maddev_program_io_regs(pmaddevobj->pDevBase, CntlReg, IntEnable, HostPA);

    iowrite32(DevLoclAddr, &pmaddevobj->pDevBase->DevLoclAddr);
    iowrite32(DmaCntl, &pmaddevobj->pDevBase->DmaCntl);
    iowrite32(DXBC, &pmaddevobj->pDevBase->DTBC);
    writeq(MAD_DMA_CDPP_END, &pmaddevobj->pDevBase->BCDPP);

    //Finally - let's go
    CntlReg |= MAD_CONTROL_DMA_GO_BIT;
    iowrite32(CntlReg, &pmaddevobj->pDevBase->Control);
    //
    maddev_enable_ints_release_lock(&pmaddevobj->devlock, flags1);

    //Wait and process the results
    iostat = maddev_get_io_status(pmaddevobj, &pmaddevobj->ioctl_q,
                                  &pmaddevobj->ioctl_f,
                                  &pmaddevobj->devlock);
    iocount = (iostat < 0) ? iostat : pmaddevobj->pDevBase->DTBC;
    if (iostat == 0)
        {ASSERT((int)(iocount == PAGE_SIZE));}

    PDEBUG("maddev_xfer_dma_page... dev#=%d iostat=%ld iocount=%ld\n",
           (int)pmaddevobj->devnum, iostat, iocount);
    pmaddevobj->ioctl_f = eIoReset;

    return iocount;
}

ssize_t maddev_xfer_sgdma_pages(PMADDEVOBJ pmaddevobj, 
                                long num_pgs, struct page* page_list[],
                                sector_t sector, bool bWr)
{
    static U32 DXBC = PAGE_SIZE;
    //
    U32 DevLoclAddr = (sector * MAD_SECTOR_SIZE);
    PMAD_DMA_CHAIN_ELEMENT pSgDmaElement = &pmaddevobj->SgDmaElements[0];
    long   loop_lmt = num_pgs - 1; 
    U32    IntEnable = 
           (bWr) ? (MAD_INT_STATUS_ALERT_BIT | MAD_INT_DMA_OUTPUT_BIT) :
                   (MAD_INT_STATUS_ALERT_BIT | MAD_INT_DMA_INPUT_BIT);
    //
    U32    DmaCntl   = MAD_DMA_CNTL_INIT;
    U32    CntlReg   = MAD_CONTROL_CHAINED_DMA_BIT;
    dma_addr_t HostPA;
    U64    CDPP;
    long   iostat;
    size_t iocount = 0;

    u32 flags1 = 0; 
    U32 flags2 = 0;
    U32 j;

    if (bWr)
        {DmaCntl |= MAD_DMA_CNTL_H2D;} //Write == Host2Disk

    PDEBUG("maddev_xfer_sgdma_pages... dev#=%d num_pgs=%d wr=%d\n",
           (int)pmaddevobj->devnum, num_pgs, bWr);

    if (num_pgs > MAD_SGDMA_MAX_PAGES)
        {
        PERR("maddev_xfer_sgdma_pages... dev#=%d num_pgs(%d) > max(%d) rc=-EINVAL\n",
             (int)pmaddevobj->devnum, num_pgs, MAD_SGDMA_MAX_PAGES);
        ASSERT((int)false);
        return -EINVAL;
        }

    pmaddevobj->ioctl_f = eIoPending;

    maddev_acquire_lock_disable_ints(&pmaddevobj->devlock, flags1);
    //
    maddev_program_io_regs(pmaddevobj->pDevBase, CntlReg, IntEnable, (phys_addr_t)0);

    //Set the base of the Chained-DMA-Pkt-Pntr list
    writeq(virt_to_phys(pSgDmaElement), &pmaddevobj->pDevBase->BCDPP);

    for (j=0; j <= loop_lmt; j++)
        {
        HostPA = maddev_page_to_dma(pmaddevobj->pdevnode, 
                                    page_list[j], PAGE_SIZE, bWr);
        if (dma_mapping_error(pmaddevobj->pdevnode, HostPA))
            {
            maddev_enable_ints_release_lock(&pmaddevobj->devlock, flags1);
            PERR("maddev_xfer_sgdma_blocks... dev#=%d returning -ENOMEM\n",
                  (int)pmaddevobj->devnum);
            ASSERT((int)false);
            return -EADDRNOTAVAIL;
            }

        //The set of hardware SG elements is defined as a linked-list even though
        //it is created as an array
        CDPP = (j == loop_lmt) ? MAD_DMA_CDPP_END : 
                                 virt_to_phys(&pmaddevobj->SgDmaElements[j+1]);

        maddev_program_sgdma_regs(pSgDmaElement, 
                                  HostPA, DevLoclAddr, DmaCntl, DXBC, CDPP);
        iocount += PAGE_SIZE;
        DevLoclAddr += PAGE_SIZE;

        //We could use the next array element but instead we use the 
        //Chained-Dma-Pkt-Pntr because we treat the chained list as a
        //linked-list to be proper 
        //pSgDmaElement = &pmaddevobj->SgDmaElements[j];
        if (j < loop_lmt)
            {pSgDmaElement = phys_to_virt(CDPP);}
        }

    //Finally - let's go
    CntlReg |= MAD_CONTROL_DMA_GO_BIT;
    iowrite32(CntlReg, &pmaddevobj->pDevBase->Control);
    //
    maddev_enable_ints_release_lock(&pmaddevobj->devlock, flags1);

    //Wait and process the results
    iostat = maddev_get_io_status(pmaddevobj, &pmaddevobj->ioctl_q,
                                  &pmaddevobj->ioctl_f,
                                  &pmaddevobj->devlock);

    iocount = (iostat < 0) ? iostat : pmaddevobj->pDevBase->DTBC;

    if (iostat == 0)
        {ASSERT((int)(iocount == (num_pgs * PAGE_SIZE)));}

    PINFO("maddev_xfer_sgdma_pages... dev#=%d num_pgs=%d iostat=%ld iocount=%ld\n",
          (int)pmaddevobj->devnum, num_pgs, iostat, iocount);
    pmaddevobj->ioctl_f = eIoReset;

    return iocount;
}

//This function services both read & write through direct-io
//We mean direct-io between the driver and the user application.
//We will implement 'buffered' io between the host and the hardware device.
//N pages will be copied across the bus - no DMA - working with the index registers
ssize_t maddev_xfer_pages_direct(PMADDEVOBJ pmaddevobj, int num_pgs, 
                                 struct page* page_list[], U32 offset, bool bWr)
{
    PMADREGS     pmadregs = pmaddevobj->pDevBase; 
    U32          IntEnable = 
                 (bWr) ? (MAD_INT_STATUS_ALERT_BIT | MAD_INT_BUFRD_OUTPUT_BIT) :
                         (MAD_INT_STATUS_ALERT_BIT | MAD_INT_BUFRD_INPUT_BIT);
    phys_addr_t  HostPA;
    U32     CntlReg;
    U32     CountBits;
    ssize_t iocount = 0;
    long    iostat;

    ASSERT((int)(pmaddevobj->devnum==pmadregs->Devnum));
    BUG_ON(pmaddevobj->devnum!=pmadregs->Devnum);

    PINFO("maddev_xfer_pages_direct... dev#=%d num_pgs=%d offset=%ld wr=%d\n",
		  (int)pmaddevobj->devnum, num_pgs, offset, bWr);

    //Declare the specific queue to have a pending io
    if (bWr) 
        {pmaddevobj->write_f = eIoPending;}
    else
        {pmaddevobj->read_f = eIoPending;}

    //If we permitted multiple device i/o's per user i/o we would set up a loop here
    CountBits = maddev_set_count_bits((PAGE_SIZE * num_pgs), MAD_CONTROL_IO_COUNT_MASK,
                                       MAD_CONTROL_IO_COUNT_SHIFT, MAD_SECTOR_SIZE);
    //Do *NOT* set the MAD_CONTROL_IOSIZE_BYTES_BIT
    CntlReg = CountBits;
    //
    HostPA = page_to_phys(page_list[0]);
    maddev_program_stream_io(&pmaddevobj->devlock, pmadregs,
                             CntlReg, IntEnable, HostPA, offset, bWr);

    //Wait for the io to complete and then process the results
    if (bWr) 
        {
        iostat = maddev_get_io_status(pmaddevobj, &pmaddevobj->write_q,
                                      &pmaddevobj->write_f,
                                      &pmaddevobj->devlock);
        if (iostat >= 0)
            {
            iocount = 
            maddev_get_io_count((U32)iostat, MAD_STATUS_WRITE_COUNT_MASK,
                                MAD_STATUS_WRITE_COUNT_SHIFT, MAD_SECTOR_SIZE);
            }
        }
    else
        {
        iostat = maddev_get_io_status(pmaddevobj, &pmaddevobj->read_q, 
                                      &pmaddevobj->read_f,
                                      &pmaddevobj->devlock);
        if (iostat >= 0)
            {
            iocount = 
            maddev_get_io_count((U32)iostat, MAD_STATUS_READ_COUNT_MASK,
                                MAD_STATUS_READ_COUNT_SHIFT, MAD_SECTOR_SIZE);
            maddev_set_dirty_pages(page_list, num_pgs);
            }
        }

    //If we have an error from the device - that's what we return
    //The specific i/o queue should already be reset in maddev_get_io_status above
    if (iostat < 0)
        {iocount = iostat;}

    //Set the specific i/o queue to ready
    if (bWr) 
        {pmaddevobj->write_f = eIoReset;}
    else
        {pmaddevobj->read_f = eIoReset;}

    PDEBUG("maddev_xfer_pages_direct... dev#=%d num_pgs=%d iocount=%ld\n",
           (int)pmaddevobj->devnum, num_pgs, iocount);

    return iocount;
}

//Determine if we need to build a Scatter-gather list
bool maddev_need_sg(struct page* pPages[], u32 num_pgs)
{
    u32 j = 0;
    phys_addr_t PA[MAD_SGDMA_MAX_PAGES+1];

    if (num_pgs > MAD_DIRECT_XFER_MAX_PAGES) 
        {return true;}

    PA[0]= page_to_phys(pPages[0]);
    if (num_pgs < 2)
        {goto NeedSgXit;}

    //If any pages are not contiguous - we must use scatter-gather
    for (j=1; j < num_pgs; j++)
        {
        PA[j] = page_to_phys(pPages[j]);
        if ((PA[j] - PA[j-1]) != PAGE_SIZE)
            {return true;}
        }

NeedSgXit:;
    #if 1
    //We can DMA a contiguous block - no need for sg-dma
    PDEBUG("maddev_need_sg... num_pgs=%ld PA0=x%llX PAx=x%llX\n",
           num_pgs, PA[0], PA[num_pgs-1]);
    #endif

    return false;
}

//This function acquires an array of page structs describing the user buffer
//and locks the buffer pages into RAM. 
//It must first acquire the mmap read-write semaphore for the owning process 
//provided by the macro (current)
long maddev_get_user_pages(U64 usrbufr, U32 nr_pages, struct page *pPages[],
                           struct vm_area_struct *pVMAs[], bool bUpdate)
{
    static long GUP_FLAGS = 
           (FOLL_TOUCH|FOLL_SPLIT|FOLL_GET|FOLL_FORCE|FOLL_POPULATE|FOLL_MLOCK);
    //
    //long gup_flags = (bUpdate) ? (GUP_FLAGS | FOLL_WRITE) : GUP_FLAGS;
    long gup_flags = (GUP_FLAGS | FOLL_WRITE); //either i/o direction
    long num_pgs;
    //int x=0; 

    down_read(&current->mm->mmap_sem);
    num_pgs = get_user_pages((u64)usrbufr, nr_pages, gup_flags, pPages, NULL);
    up_read(&current->mm->mmap_sem);

    #if 0
    PDEBUG("maddev_get_user_pages... num_pgs:x=%d r=%d pPgs=%px pPS0=%px pPS1=%px pPS2=%px\n",
           nr_pages, num_pgs, pPages, pPages[0], pPages[1], pPages[2]);
    while (pPages[x] != NULL)
        {
        PDEBUG("maddev_get_user_pages... pPSx=%px PAx=x%llX flagsx=x%X\n",
               pPages[x], page_to_phys(pPages[x]), pPages[x]->flags);
        x++;
        }
    #endif

    return num_pgs;
}

void maddev_put_user_pages(struct page** ppPages, u32 num_pgs)
{
    #if 1
    struct page* pPage = *ppPages; 
    PDEBUG("maddev_put_user_pages... num_pgs=%d pPages=%px pPS0=%px PA0=x%llX\n",
           (int)num_pgs, ppPages, pPage, virt_to_phys(pPage));
    #endif

    {
    u32 j; 
    down_read(&current->mm->mmap_sem);
    //put_user_pages(ppPages, num_pgs);
    struct page* pPage = *ppPages;
    for (j=1; j <= num_pgs; j++)
        {
        put_page(pPage);
        pPage++;
        }

    up_read(&current->mm->mmap_sem);
    }

    return;
}

//This function programs the hardware for a buffered io.
//A chunk of data is going across the bus to the device.
void maddev_program_stream_io(spinlock_t *splock, PMADREGS pmadregs,
		                      U32 ControlReg, U32 IntEnableReg, 
                              phys_addr_t HostAddr, U32 offset, bool bWr)
{
	U32 CntlReg = ControlReg;
   	u32 flags1 = 0;
    U32 flags2 = 0;

    PINFO("maddev_program_stream_io... dev#=%d PA=x%llX CntlReg=x%X IntEnable=x%X offset=x%X wr=%d\n",
          (int)pmadregs->Devnum, HostAddr, ControlReg, IntEnableReg, offset, bWr);        

    ASSERT((int)(pmadregs != NULL));

    maddev_acquire_lock_disable_ints(splock, flags1);
    //
	maddev_program_io_regs(pmadregs, CntlReg, IntEnableReg, HostAddr);

    if ((CntlReg & MAD_CONTROL_IOSIZE_BYTES_BIT) == 0)
        {
        ASSERT((int)((long)offset >= 0));
        if (bWr) 
            {pmadregs->ByteIndxWr = offset;}
        else
            {pmadregs->ByteIndxRd = offset;}
        }

    //Write the 'go' bit to the hardware after programming the other registers
	CntlReg |= MAD_CONTROL_BUFRD_GO_BIT;
	iowrite32(CntlReg, &pmadregs->Control);
	//
    maddev_enable_ints_release_lock(splock, flags1);

    PINFO("maddev_program_stream_io... dev#=%d exit\n", (int)pmadregs->Devnum);        

	return;
}

//This function resets the hardware device to a standard state after an i/o
void maddev_reset_io_registers(PMADREGS pmadregs, spinlock_t *splock)
{
	u32 Status;
   	u32 flags1 = 0;
    U32 flags2 = 0;
    U32 IoTag;

    BUG_ON(!(virt_addr_valid(pmadregs)));

    if (splock != NULL)
        {maddev_acquire_lock_disable_ints(splock, flags1);}

    iowrite32(0, &pmadregs->MesgID);
    iowrite32(MAD_CONTROL_RESET_STATE, &pmadregs->Control);

    Status = ioread32(&pmadregs->Status);
    Status &= ~MAD_STATUS_ERROR_MASK;
    iowrite32(Status, &pmadregs->Status);

    iowrite32(0, &pmadregs->IntID);
    iowrite32(0, &pmadregs->IntEnable);

    IoTag = ioread32(&pmadregs->IoTag);
    IoTag++;
    iowrite32(IoTag, &pmadregs->IoTag);

	if (splock != NULL)
        {maddev_enable_ints_release_lock(splock, flags1);}

    PDEBUG("maddev_reset_io_registers... dev#=%d ok\n", (int)pmadregs->Devnum);
	return;
}

//This function converts a hardware error to a linux-specific error code
int maddev_status_to_errno(int devnum, PMADREGS pmadregs)
{
int rc = 0;
U32 StatusReg; 

    BUG_ON(!(virt_addr_valid(pmadregs)));

    StatusReg = (pmadregs->Status & ~MAD_STATUS_CACHE_INIT_MASK);
	if ((pmadregs->IntID & MAD_INT_STATUS_ALERT_BIT) == 0)
        return 0;

    switch(StatusReg)
        {
        case MAD_STATUS_NO_ERROR_MASK: //No Status bits set ?!?
            rc = -EBADE;               //Bad exchange - device disagrees w/ itself ?
            break;

        case MAD_STATUS_GENERAL_ERR_BIT: //The device indicates a general error
           	rc = -EIO;
           	break;

        case MAD_STATUS_OVER_UNDER_ERR_BIT: //The device indicates an overflow/underflow
           	rc = -ENODATA;
           	if (pmadregs->IntID & MAD_INT_OUTPUT_MASK)
           		rc = -EOVERFLOW;

            //If the cache was empty... Try again after priming the cache
           	if (pmadregs->Control & MAD_CONTROL_CACHE_XFER_BIT) 
           		rc = -EAGAIN;                                   
            break;

        case MAD_STATUS_DEVICE_BUSY_BIT: //The device indicates a busy state
          	rc = -EBUSY;
           	break;

        case MAD_STATUS_DEVICE_FAILURE_BIT: //The device indicates an internal failure
           	rc = -ENOTRECOVERABLE;
           	break;

        case MAD_STATUS_INVALID_IO_BIT:
          	rc = -ENOEXEC;
            break;

        case MAD_STATUS_ERROR_MASK: //All remaining bits on after anding above
            //Assumed power failure (all bits on)  
          	ASSERT((int)(pmadregs->Status == (U32)-1));
            ASSERT((int)(pmadregs->IntID == (U32)-1));
          	rc = -ENODEV; //Well - not anymore
            break;

        default:
          	PWARN("maddev_status_to_errno: undefined device error!... dev#=%d IntID=x%X Control=x%X, Status=x%X\n",
   	              devnum, (unsigned int)pmadregs->IntID, (unsigned int)pmadregs->Control, 
   	              (unsigned int)pmadregs->Status);
   	        rc = -EIO;
        }

    if (rc != 0)
        PWARN("maddev_status_to_errno... dev#=%d rc=%d\n", devnum, rc);

	return rc;
}

//This function waits for the DPC to signal/post the event (wakeup the i/o queue)
//and then retrieves the status register 
long maddev_get_io_status(PMADDEVOBJ pmdo, wait_queue_head_t* io_q,
                          long* io_f, spinlock_t* plock)
{
    PMADDEVOBJ pmaddevobj = pmdo;
    PMADREGS   pIsrState  = &pmaddevobj->IntRegState;
    long iostat = 0;
    U32 flags1 = 0;
    U32 flags2 = 0;

    //Wait for the i/o-completion event signalled by the DPC
    wait_event(*io_q, (*io_f != eIoPending));

    //Do we have a completed i/o or an error condition ?
    if (*io_f != eIoCmplt)
        {iostat = *io_f;} //Should be negative
    else
        {
        //Process the i/o completion
        iostat = maddev_status_to_errno(pmaddevobj->devnum, pIsrState);

        //If we are working with user pages we must do the simulation
        //buffer copying in the user thread context ...
        //even though it worked fine inside the simulator for the 
        //simple case of buffered i/o (copyfromuser, copytouser)
        //These function(s) are found in the simulator mbdevthread.C 
        #ifdef _MAD_SIMULATION_MODE_
        pmaddevobj->pSimParms->pcomplete_simulated_io(pmaddevobj->pSimParms->pmadbusobj,
                                                      pIsrState);
        #endif
        }

    PDEBUG("maddev_get_io_status... dev#=%d iostat=x%X\n",
           pmaddevobj->devnum, iostat);
    return iostat;
}

//This function initializes the MAD device object
void maddev_init_io_parms(PMADDEVOBJ pmaddevobj, U32 indx)
{
    ASSERT((int)(pmaddevobj != NULL));
    //
    pmaddevobj->devnum = indx;
    pmaddevobj->read_f = eIoReset;
    pmaddevobj->write_f = eIoReset;
    pmaddevobj->ioctl_f = eIoReset;

    #ifdef _BLOCKIO_
	tasklet_init(&pmaddevobj->dpctask, maddevb_dpctask, indx);
    #endif

    #ifdef _CDEV_
	tasklet_init(&pmaddevobj->dpctask, maddevc_dpctask, indx);
    #endif

    init_waitqueue_head(&pmaddevobj->read_q);
	init_waitqueue_head(&pmaddevobj->write_q);
	init_waitqueue_head(&pmaddevobj->ioctl_q);
	//INIT_WORK(&pmaddevobj->dpc_work_rd, maddev_dpcwork_rd);
    //INIT_WORK(&pmaddevobj->dpc_work_wr, maddev_dpcwork_wr);
}

//This function returns a kernel virtual address from an imput phys addr
void* maddev_getkva(phys_addr_t PhysAddr, struct page** ppPgStr)
{
    phys_addr_t pfn = ((PhysAddr >> PAGE_SHIFT) & PFN_AND_MASK); // sign propogates Hi -> Lo  *!?!*
 
    *ppPgStr = pfn_to_page((int)pfn);
    ASSERT((int)(*ppPgStr != NULL));

#ifdef _MAD_SIMULATION_MODE_       //Assume the simulator kmalloc'd our device in RAM
                                   // so a virtual mapping exists 
    return page_address(*ppPgStr); //We compare w/ what the simulator reports for sanity 
#else 
    return kmap(*ppPgStr); //We assume our real hardware is mapped into phys-addr space
#endif
}

//This function unmaps a kernel virtual addr if necessary
void maddev_putkva(struct page* pPgStr)
{
    ASSERT((int)(pPgStr != NULL));
#ifndef _MAD_SIMULATION_MODE_ //We assume that we kmap'd our real hardware into kernel VA
    kunmap(pPgStr);
#endif
}

#ifdef _MAD_SIMULATION_MODE_ /////////////////////////////////////////
//
//This function exchanges necessary parameters w/ the simulator
extern PMAD_SIMULATOR_PARMS madbus_xchange_parms(int num);

int maddev_xchange_sim_parms(struct pci_dev* pPciDev, PMADDEVOBJ pmaddevobj)
{
    PMAD_SIMULATOR_PARMS pSimParms;
    int rc = 0;

    ASSERT((int)(pPciDev != NULL));
    ASSERT((int)(pmaddevobj != NULL));

    pSimParms = madbus_xchange_parms((int)pmaddevobj->devnum);
    if (pSimParms == NULL)
        { 
        PWARN("maddev_xchange_sim_parms... dev#=%d rc=-ENXIO\n",
              (int)pmaddevobj->devnum);
        return -ENXIO;
        }

    pmaddevobj->pSimParms = pSimParms;
    pSimParms->pmaddevobj = pmaddevobj;
    pSimParms->pdevlock   = &pmaddevobj->devlock;

    PINFO("maddev_xchange_sim_parms... dev#=%d pmbobj=%px\n",
          (int)pmaddevobj->devnum, pSimParms->pmadbusobj);

    return rc;
}
#endif //_MAD_SIMULATION_MODE_ ///////////////////////////////////

int maddev_kobject_init(struct kobject* pkobj, struct kobject* pPrnt,
                        struct kset* kset, struct kobj_type* ktype, 
                        const char *objname)
{
    int rc = 0;

    ASSERT((int)(pkobj != NULL));
    memset(pkobj, 0x00, sizeof(struct kobject));
    //
    rc = kobject_init_and_add(pkobj, &mad_ktype, pPrnt, objname);
    //pkobj->state_initialized = 0;
    PDEBUG("maddev_kobject_init(_and_add)... rc=%d\n", rc);
    return rc;

    pkobj->parent = pPrnt;
    pkobj->kset   = kset;
    pkobj->ktype  = ktype;
    if (pPrnt != NULL)
        {
        BUG_ON(pPrnt->sd == NULL);
        ;
        BUG_ON(pPrnt->sd->parent == NULL);
        //pkobj->sd = 
            //kernfs_create_link(pPrnt->sd->parent, objname, pPrnt->sd);
        }
    kobject_set_name(pkobj, objname);
    //kobject_init(pkobj, ktype);
    return rc;
}

int maddev_kobject_register(struct kobject* pkobj, struct kobject* pPrnt, const char *objname)
{
    int rc; 

    //rc = kobject_set_name(pkobj, objname);
    //if (rc == 0)
    //{rc = kobject_register(pkobj);}
    memset(pkobj, 0x00, sizeof(struct kobject));
    rc = kobject_init_and_add(pkobj, &mad_ktype, pPrnt, objname);
    if (rc != 0)
        {
        kobject_put(pkobj);
        PERR("mad_kobject_register failed!... rc=%d\n", rc);
        }

    return rc;
}
//
void maddev_kobject_unregister(struct kobject* pkobj)
{
    //kobject_unregister(pkobj);
    kobject_del(pkobj);
    kobject_put(pkobj);
}
//
void maddev_kset_unregister(void)
{
    if (bMadKset)
        {maddev_kobject_unregister((struct kobject*)&mad_kset);}
}
//
int maddev_kset_create()
{
    int rc;

    rc = maddev_kobject_register((struct kobject*)&mad_kset, NULL, MadKsetNm);
    bMadKset = (rc == 0);

    return rc;
}

