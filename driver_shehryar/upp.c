/* 
 *   A device driver for the Texas Instruments
 *   Universal Paralllel Port (UPP)
 *  
 *   
 *
 *   Modified by: Ali Shehryar <github.com/sshehryar>; 
 */
  
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/fs.h>      
#include <linux/delay.h>      //for "mdelay(...)"
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/moduleparam.h>
#include <mach/da8xx.h>
#include <asm/sizes.h>
#include <asm/io.h>       
#include <mach/mux.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <asm/gpio.h>

//SPRUH77A.PDF, Table 12-1. The Interrupt number assigned to the UPP module.
#define UPP_INTERRUPT   91

//SPRS586D.PDF, Table 2-4
#define DA850_UPP_BASE  0x01E16000

//SPRS586D.PDF, Table 5-117. Offsets from DA850_UPP_BASE
#define UPPCR		0x00000004
#define UPDLB           0x00000008
#define UPCTL           0x00000010
#define UPICR           0x00000014
#define UPIVR           0x00000018
#define UPTCR           0x0000001C
#define UPIER           0x00000024
#define UPIES           0x00000028
#define UPIEC           0x0000002C
#define UPEOI           0x00000030
#define UPID0           0x00000040
#define UPID1           0x00000044
#define UPID2           0x00000048
#define UPIS0           0x00000050
#define UPIS1           0x00000054
#define UPIS2           0x00000058
#define UPQD0           0x00000060
#define UPQD1           0x00000064
#define UPQD2           0x00000068
#define UPQS0           0x00000070
#define UPQS1           0x00000074
#define UPQS2           0x00000078

//SPRS586D.PDF, Table 2-4
#define DA850_PSC1_BASE 0x01E27000 

//SPRUH77A.PDF, Table 9-7. 
//"Power and Sleep Controller 1" (PSC1)  Revision ID Register.
#define PSC_REVID       0x00000000

//SPRUH77A.PDF, Table 9-7. 
//"Power Domain Transition Status" Register.
#define PSC_PTSTAT      0x00000128

//SPRUH77A.PDF, Table 9-2, Table 9-7. 
//NOTE that in Table 9-2, UPP module has an LPSC number of 19...
#define PSC1_MDCTL_19   0x00000A4C  //0xA00 + (19*4). 

//SPRUH77A.PDF, Table 9-7. 
//"Power Domain Transition Command Register" Register.
#define PSC_PTCMD       0x00000120



 
//DMA Status Register bitmasks used in the ISR handler.... 
#define EOLI   16
#define EOWI   8
#define ERRI   4
#define UORI   2 
#define DPEI   1
#define EOLQ   (1 << 12)
#define EOWQ   (1 << 11)   
#define ERRQ   (1 << 10)
#define UORQ   (1 << 9)
#define DPEQ   (1 << 8)

#define UPIES_MASK 0x1717
///#define UPIES_MASK 0x1717 ---> Reverted back to 1717 because Window interrupt is not used, for now its useless
//To Enable all interrupts --->  #define UPIES_MASK 0x1F1F



/// Shehryar: These Parameters are to be modified and tweaked according to reqirements in realtime.
//The DMA PARAMETERS 
#define UPP_BUF_SIZE       8192
#define UPP_RX_LINE_COUNT  1
#define UPP_RX_LINE_SIZE   8192
//#define UPP_RX_LINE_OFFSET UPP_RX_LINE_SIZE
#define UPP_RX_LINE_OFFSET 0
#define UPP_TX_LINE_COUNT  1
#define UPP_TX_LINE_SIZE   1024
//#define UPP_TX_LINE_OFFSET UPP_TX_LINE_SIZE
#define UPP_TX_LINE_OFFSET 0
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//MODIFIED BY: SHEHRYAR ---> Pg 888 General-Purpose Input/Output (GPIO) SPRUH82A–December 2011. Added 25-Nov-2014			
																	
#define DA850_GPIO_BASE       0x01E26000
													

//MODIFIED BY: SHEHRYAR. Offsets from GPIO_Base (SPRS653C.PDF TABLE 5-134)

#define DIR67		0x00000088
#define OUT_DATA67	0x0000008C
#define SET_DATA67	0x00000090
#define CLR_DATA67	0x00000094	
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void *rxBuf; 
static void __iomem *pinmux_base = 0;
static void __iomem *upp_base    = 0;
static void __iomem *psc1_base   = 0;
static void __iomem *gpio_base   = 0;

static DECLARE_WAIT_QUEUE_HEAD(read_queue);
static int read_pending = 0;
static DECLARE_WAIT_QUEUE_HEAD(write_queue);
static int write_pending = 0;




//set to '1' when loading this module to use the Digital Loopback (DLB) 
//features,e.g:
//"insmod UPP_driver.ko loopbackMode=1"

static int loopbackMode = 0;

module_param( loopbackMode, int, S_IRUGO);

// This function can be removed after active development is complete.//////////////////////////////////////////////////
// simply "dumps" the contents of the relevant registers to the console////////////////////////////////////////////////
// for debugging/development purposes.////////////////////////////////////////////////////////////////////////////////
static void dump_Channel_regs( void )
{
   uint32_t regVal;

   regVal = ioread32( upp_base + UPIS0 );
   printk(KERN_INFO "\n\n");
   printk(KERN_INFO "UPIS0=0x%08X (Current address of Channel I).\n", regVal);
   regVal = ioread32( upp_base + UPIS1 );
   printk(KERN_INFO "UPIS1=0x%08X (Line-Count=%d and Byte-Count=%d).\n", regVal, regVal >> 16, regVal & 0xFFFF);
   regVal = ioread32( upp_base + UPIS2 );
   printk(KERN_INFO "UPIS2=0x%08X (ACTIVE=%d, PENDING=%d, WATERMARK=%d)\n", regVal, regVal & 0x01, regVal & 0x02, regVal >> 4);


   

   regVal = ioread32( upp_base + UPID0 );
   printk(KERN_INFO "UPID0=0x%08X (BASE ADDRESS)\n", regVal);
   regVal = ioread32( upp_base + UPID1 );
   printk(KERN_INFO "UPID1=0x%08X (LINE COUNT=%d, BYTE COUNT = %d)\n", regVal, regVal >> 16, regVal & 0xFFFF);
   regVal = ioread32( upp_base + UPID2 );
   printk(KERN_INFO "UPID2=0x%08X (CHANNEL LINE OFFSET)\n", regVal);

   
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////INTERRUPT SERVICE ROUTINE//////////////////////////////////////////////////////////////////////////
///////////////////////// SPRUH82A–December 2011 PAGE 1515////////////////////////////////////////////////////////////////////////////////

   //SPRUGJ5B.PDF, Section 2.6.4
static irqreturn_t upp_ISR_handler(int irq, void *dev_id)
{
   uint32_t regVal, status;

   if (pinmux_base == 0) 
   {
      return IRQ_HANDLED;
   }

   status = ioread32( upp_base + UPIER );

 
   while (status &  0x1F1F )  //0x1F1F is an interrupt bit-mask 
   {
      //
      //DMA Channel I (Channel A), Receiving data (We Need A (DMA Ch Q ) to Rx Data instead of Tx ; 27th Nov 2014 - 10:38am)
      //
      if (status & EOLI) 
      {
         //Clear the interrupt. WRITING ZERO to any other bit has NO effect,
         //per SPRUH77A.PDF, section 33.3.9.
         iowrite32(EOLI, upp_base + UPIER );

         read_pending = UPP_RX_LINE_SIZE; // this should be read_pending b/z chA/DMA I set to RX
         wake_up_interruptible(&read_queue);

         //printk(KERN_INFO "DMA:EOLI\n");
	printk(KERN_INFO "DMA:EOLI.  UPP_RX_LINE_SIZE[%d] UPP_RX_LINE_OFFSET[%d] UPP_RX_LINE_COUNT[%d] \n", UPP_RX_LINE_SIZE, UPP_RX_LINE_OFFSET,UPP_RX_LINE_COUNT );
		 //dump_Channel_regs();
      }
      if (status & EOWI) 
      {
         //Clear the interrupt. WRITING ZERO to any other bit has NO effect,
         //per SPRUH77A.PDF, section 33.3.9.
         iowrite32(EOWI, upp_base + UPIER );

         write_pending = UPP_RX_LINE_SIZE;
         wake_up_interruptible(&read_queue);

         printk(KERN_INFO "DMA:EOWI\n");
         //dump_Channel_regs();
      }
      if (status & ERRI) 
      {
         //Clear the interrupt. WRITING ZERO to any other bit has NO effect,
         //per SPRUH77A.PDF, section 33.3.9.
         iowrite32(ERRI, upp_base + UPIER );

         printk(KERN_INFO "DMA Channel I *ERROR*.\n");
         dump_Channel_regs();
      }
      if (status & UORI) 
      {
         //Clear the interrupt. WRITING ZERO to any other bit has NO effect,
         //per SPRUH77A.PDF, section 33.3.9.
         iowrite32(UORI, upp_base + UPIER );

         printk(KERN_INFO "DMA Channel I Underrun/Overflow.\n");
         dump_Channel_regs();
      }
      if (status & DPEI) 
      {
         //Clear the interrupt. WRITING ZERO to any other bit has NO effect,
         //per SPRUH77A.PDF, section 33.3.9.
         iowrite32(DPEI, upp_base + UPIER );

         printk(KERN_INFO "DMA Channel I Programming Error (DPE).\n");
         dump_Channel_regs();
      }
   }	
   //Clear UPEOI to allow future calls to this function.
   regVal = ioread32( upp_base + UPEOI);
   regVal &= 0xFFFFFF00;
   regVal = 0;// End of Interrupt
   iowrite32(regVal, upp_base + UPEOI);

   return IRQ_HANDLED;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void pin_mux_set( int index, unsigned int bits )
{
   static DEFINE_SPINLOCK(mux_spin_lock);
   unsigned long flags;
   unsigned int offset;

   if ((index < 0) || (index > 19))
   {
      printk(KERN_INFO "pin_mux_set:index is out of range.\n");
      return;
   }

   if (!pinmux_base) 
   {
      //SRPUH77A.PDF,Table 11-3
      if ((pinmux_base = ioremap(DA8XX_SYSCFG0_BASE, SZ_4K)) == 0) 
      {
         printk(KERN_INFO "pin_mux_set:Cannot fetch pinmux_base.\n");
         return;
      }
   }

   offset = 0x120 + (index * 4);
   spin_lock_irqsave(&mux_spin_lock, flags);
   iowrite32(bits, pinmux_base + offset);
   spin_unlock_irqrestore(&mux_spin_lock, flags);

   //NOTE: do NOT "iounmap" the pinmux_base pointer, as it is used
   //      in the ISR_handler.....
}



static void upp_pin_mux_init(void)
{
   pin_mux_set( 13, 0x44440000 );
   pin_mux_set( 14, 0x44444480 );
   pin_mux_set( 15, 0x44444444 );
   pin_mux_set( 16, 0x44444444 );
   pin_mux_set( 17, 0x44444444 );
   pin_mux_set( 18, 0x00444444 );
   pin_mux_set( 19, 0x08888800 );	
   //pin_mux_print();
}


   //SPRUGJ5B.PDF, Section 2.6.1.2.
static void upp_power_and_clocks( void )
{
   /* 
    * Refer to: 
    *   * Power and Sleep Controller (PSC), Chapter 9 in the TRM(SPRUH77A.PDF)
    *   * Device Clocking, Chapter 7 (esp Section 7.3.5) in the TRM. 
    *  
    *  
    */
    int regVal;


    if (!psc1_base) 
    {
       if ((psc1_base = ioremap(DA850_PSC1_BASE, SZ_4K)) == 0)
       {
          printk(KERN_INFO "upp_power_and_clocks:Cannot fetch psc1_base.\n");
          return;
       }
    }

    regVal = ioread32(psc1_base + PSC_REVID);

    //PSC Revision ID should be "44825A00" per SPRUH77A.PDF, section 9.6.1
    if (regVal == 0x44825A00) 
    {
       printk( KERN_INFO "PSC_REVID = 0x%08X....OK\n", regVal); 
    }
    else
    {
       printk( KERN_INFO "********ERROR: PSC_REVID = 0x%08X********\n", regVal); 
    }

    // SPRUH77A.PDF, 9.3.2.1, Table 9-6, 9.6.10 
    // wait for GOSTAT[0] in PSTAT to clear to 0 ("No transition in progress")
    while ( ioread32(psc1_base + PSC_PTSTAT) & 0x00000001 )
    ;   

    //
    //SPRUH77A.PDF, 9.3.2.2,  9.6.19.
    //Set NEXT bit in MDCTL19 to Enable(3h).
    regVal  = ioread32( psc1_base + PSC1_MDCTL_19 );
    regVal |= 0x00000003;
    iowrite32(regVal, psc1_base + PSC1_MDCTL_19);


    //
    //SPRUH77A.PDF, 9.3.2.3,  9.6.9. 
    //Set the GO[0] bit in PTCMD to 1 to initiate power-domain transition
    regVal  = ioread32(psc1_base + PSC_PTCMD);
    regVal |= 0x00000001;
    iowrite32(regVal, psc1_base + PSC_PTCMD);
    
    //
    // SPRUH77A.PDF, 9.3.2.4 
    // Wait for GOSTAT[0] in PTSTAT to clear to 0
    while ( ioread32(psc1_base + PSC_PTSTAT) & 0x00000001 )
    ;  

    iounmap( psc1_base );
    psc1_base = 0;
    printk( KERN_INFO "Clock and Power Configuration....OK\n"); 

}

   //SPRUGJ5B.PDF, Section 2.6.1.3, 2.6.1.4
static void upp_swrst( void )
{
    int32_t reg_val;

    if (!upp_base)
    {
       if ((upp_base = ioremap(DA850_UPP_BASE, SZ_4K)) == 0) 
       {
          printk(KERN_INFO "upp_swrst:Cannot fetch upp_base.\n");
          return;
       }
    }
    //Fetch the UPP ID for the sake of sanity....Should be "44231100"
    reg_val = ioread32( upp_base + 0 );
    if (reg_val == 0x44231100 ) 
    {
       printk( KERN_INFO "UPP_UPPID = 0x%08X....OK\n", reg_val);
    }
    else
    {
       printk( KERN_INFO "********ERROR: UPP_UPPID = 0x%08X********\n", reg_val); 
    }



    // SPRUH77A.PDF, Section 33.2.7.1.1, Table 33-12.
    // clear EN bit of UPPCR to (temporarily) disable the UPP. 
    reg_val = ioread32( upp_base + UPPCR );
    reg_val &= ~(1 << 3);             //0xfffffff7;
    iowrite32( reg_val, upp_base + UPPCR );

    // SPRUH77A.PDF, Section 33.2.7.1.2, Table 33-12.
    //poll "DMA Burst" (DB) bit of UPPCR to ensure DMA controller is idle
    while ( ioread32( upp_base + UPPCR ) & (1 << 7) )
        ;


    // SPRUH77A.PDF, Section 33.2.7.1.3, Table 33-12.
    // assert SWRST bit (bit 4) of UPPCR
    reg_val  = ioread32( upp_base + UPPCR );
    reg_val |= 0x00000010;
    iowrite32( reg_val, upp_base + UPPCR );

    //
    // wait at least 200 clock cycles 
    // (SPRUGJ5B.PDF, 2.6.1.4)
    mdelay( 200 );  // abitrary choice of 200ms


    // SPRUH77A.PDF, Section 33.2.7.1.4  --AND--
    // SPRUGJ5B.PDF, 2.6.1.4
    // clear SWRST bit (bit 4) of UPPCR
    reg_val = ioread32( upp_base + UPPCR );
    reg_val &= 0xffffffef;
    iowrite32( reg_val, upp_base + UPPCR );

    printk(KERN_INFO "upp_swrst....OK\n");
}


   //SPRUGJ5B.PDF, Section 2.6.1.5
static void upp_config( void )
{
   int32_t regVal;

   //-------------------------------------------------------------------------
   // UPPCTL - UPP Interface Channel Settings....SPRUH77A.PDF, Section 33.3.4.
   //        
   //        - DATA and XDATA Pin assignments to Channels A & B:
   //          Refer to SPRUGJ5B.PDF, Table 3: 
   //              
   //        ____PHYSICAL_PINS___|____CHANNEL_ASSIGNMENT___
   //          * DATA[7:0]       |       A[7:0]
   //          * DATA[15:8]      |       B[7:0]
   //-------------------------------------------------------------------------
   regVal = 0;
   regVal = 0;			// OPERATING MODE: Changed to All Receive Mode ; Modified by: Shehryar on 24-11-2014
   regVal = 0;			// Channel A is active
   regVal |= 1 << 17;		// IWA  - CHANNEL A 8/16bit MODE: Set Channel A to 16 bit mode
   //regVal &= ~(1<<18);	// DPWA - CHANNEL A Bit Width Mode: Set 16 bit Mode, combined with IWA
   //regVal &= ~(1<<21);	// DPFA - CHANNEL A Data Packing Mode: Set Right-justified, zero extended, Has no impact on 8/16 bit modes
   

   iowrite32( regVal, upp_base + UPCTL );


   //-------------------------------------------------------------------------
   // UPPICR - signal enable, signal inversion, clk div (tx only), etc.
   //          SPRUH77A.PDF, Section 33.3.5
   //-------------------------------------------------------------------------
   
   regVal  = 0;        //Channel A: START is active-high
   regVal  = 0;        //Channel A: ENABLE is active-high
   regVal  = 0;        //Channel A: WAIT is active-high
   regVal |= 1<<3;     // Channel A: honors WAIT for Start
   regVal |= 1<<4;     //Enable signal is enabled and starts in Rx mode
   regVal |= 1<<5;   //Channel A: honors WAIT in transmit mode.
   //regVal |= 0xF<<8; //Channel A: Clock Divisor, to slow down ... Slowest XMIT clock possible.
   //regVal |= 1<<12;  //Channel A:(CLKINVA) Signal on rising edge of clock
   //regVal |= 1<<13;  //Channel A:(TRISA) pins are High-impedence while idle

   iowrite32( regVal, upp_base + UPICR );


   //-------------------------------------------------------------------------
   // UPPIVR - Idle Value Register
   //          SPRUH77A.PDF, Section 33.3.5
   //-------------------------------------------------------------------------
   regVal = 0;
   regVal |= 0xab00;   //Channel B Idle Value
   regVal |= 0x00cd;   //Channel A Idle Value
   iowrite32( regVal, upp_base + UPIVR );


   //-------------------------------------------------------------------------
   // UPTCR - i/o tx thresh (tx only), dma read burst size
   //-------------------------------------------------------------------------
   regVal  = 0x03;            //DMA Channel I READ-threshold. 256 bytes (max)
   regVal |= 0x03 << 8;       //DMA Channel Q READ-threshold. 256 bytes
   regVal |= 0x03 << 16;      //    Channel A XMIT-threshold. 256 bytes
   regVal |= 0x03 << 24;      //    Channel B XMIT-threshold. 256 bytes
   iowrite32( regVal, upp_base + UPTCR );


   //-------------------------------------------------------------------------
   // UPPDLB - digital loopback
   //-------------------------------------------------------------------------


   if (loopbackMode == 0) 
   {
      //Disable ALL loopback paths.
      regVal  = 0;
   } else
   {
      //Enable A->B Loopback
      regVal  = 0;
      regVal |= 1 << 12;
      printk(KERN_INFO "***********WARNING! Using LOOPBACK mode**************\n");
   }
   iowrite32( regVal, upp_base + UPDLB );

   printk(KERN_INFO "upp_config....OK\n");
}

//SPRUGJ5B.PDF, Section 2.6.1.6
static void upp_interrupt_enable( void )
{
   int32_t regVal, status;

   // Register the ISR before enabling the interrupts....
   status = request_irq( UPP_INTERRUPT, upp_ISR_handler, 0, "upp_ISR", 0 );
   if( status < 0 ) 
   {
        printk( KERN_INFO "upp_interrupt_enable: error requesting UPP IRQ %d\n", status );
        return;
   }
   
   // clear all interrupts
   iowrite32( UPIES_MASK, upp_base + UPIEC );

   //-------------------------------------------------------------------------
   // UPIES - Interrupt Enable. Interrupt events will generate a CPU interrupt
   //-------------------------------------------------------------------------
   regVal  = 0x17;            //Enable ALL interrupts (but EOWI) for DMA Channel I
   //regVal |= 0x17 << 8;       //Enable ALL interrupts (but EOWQ) for DMA Channel Q 
   regVal = UPIES_MASK;
   iowrite32( regVal, upp_base + UPIES );

   printk(KERN_INFO "upp_interrupt_enable....OK\n");
}

static void upp_interrupt_clear( void )
{
	// clear all interrupts
	iowrite32( UPIES_MASK	, upp_base + UPIEC );
	//iowrite32( 0			, pinmux_base + UPEOI);
}
static void upp_interrupt_set( void )
{
	// set all interrupts
	iowrite32( UPIES_MASK, upp_base + UPIES );
}
static void upp_interrupt_reset( void )
{
	upp_interrupt_clear();
	upp_interrupt_set();
}

//SPRUGJ5B.PDF, Section 2.6.1.7
static void upp_enable( void )
{
    int32_t reg_val;

    // set EN bit in UPPCR. 
    // The EN bit (effectively disabling the UPP peripheral)...
    // was cleared in "upp_swrst()" function
    reg_val = ioread32( upp_base + UPPCR );
    reg_val |=  1 << 3;  
    iowrite32( reg_val, upp_base + UPPCR );

    printk(KERN_INFO "upp_enable....OK\n");
}

static void upp_disable( void )
{
	int32_t reg_val;
	
	reg_val = ioread32( upp_base + UPPCR );
	reg_val &= ~(1 << 3);             //0xfffffff7;
	iowrite32( reg_val, upp_base + UPPCR );
	
	printk(KERN_INFO "upp_disabled\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////MODIFIED BY: SHEHRYAR ; 25-NOV-2014 4:40pm PST (+5.00 GMT)//////////////////////////////////////////////////////////////////////
static void setpin_GPIO (void)
{

	int32_t reg_val;
	reg_val = ioread32(gpio_base + SET_DATA67);
	reg_val |= (1<<6); ///Set Pin 6 of Bank 6 GP6P6 to 1 to drive GPIO high
	iowrite32(reg_val,gpio_base + SET_DATA67);
	

	
}

static void clrpin_GPIO(void){

	int32_t reg_val;
	reg_val = ioread32(gpio_base + CLR_DATA67);
	reg_val |= (1<<6); //Set Pin 6 of bank 6 GP6P5 of CLR_DATA67 Register to High to drive GPIO signals low
	iowrite32(reg_val,gpio_base + CLR_DATA67);
}
///////Function to set DIR to 1 for GP6P5

static void Config_GPIO(void){

	gpio_base = ioremap(DA850_GPIO_BASE, SZ_4K);
	
//set dir 
	int32_t reg_val;
	reg_val = ioread32(gpio_base + DIR67);
	reg_val &= ~(1<<0); 
	reg_val &= ~(1<<1);
	reg_val &= ~(1<<2);
	reg_val &= ~(1<<3);
	reg_val &= ~(1<<4);
	reg_val &= ~(1<<6);
	iowrite32(reg_val,gpio_base + DIR67);
//set to high

	reg_val = ioread32(gpio_base + SET_DATA67);
	reg_val |= (1<<0); 
	reg_val |= (1<<1);
	reg_val |= (1<<2);
	reg_val |= (1<<3);
	reg_val |= (1<<4);
	reg_val |= (1<<6);
	iowrite32(reg_val,gpio_base + SET_DATA67);


}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//SPRUGJ5B.PDF, Section 2.6.1.8
// Return false on error
static bool upp_mem_alloc( void )
{
   /* NOTE: An experiment to try is to place the UPP buffers in L2 or L3
            RAM, to decrease the transfer times if there is a problem...
   */

  
   
   rxBuf = kcalloc( 1 , UPP_BUF_SIZE, GFP_KERNEL | GFP_DMA  );
   if (!rxBuf) 
   {
      printk(KERN_INFO "upp_mem_alloc: rxBuf FAILED!\n");
      return false;
   }

   printk(KERN_INFO "upp_mem_alloc....OK\n");
   return true;
}

//Need to Ensure Changes to DMA Channels such that CHANNEL A is SET TO RX and CHANNEL B is set To TX; 25-Nov-2014 11:13am (+5.00 GMT)
static void upp_program_DMA_channelA( void )
{
	//------------------------------------------------------------------------
	// Channel A (Rx), (DMA Channel I) //27th Nov 104 - 11:08 am
	//------------------------------------------------------------------------
	iowrite32( rxBuf, upp_base + UPID0);  
	iowrite32( ( UPP_RX_LINE_COUNT << 16 | UPP_RX_LINE_SIZE ), upp_base + UPID1);
	iowrite32( UPP_RX_LINE_OFFSET, upp_base + UPID2);
}



static void upp_program_DMA_channels( void )
{
	upp_program_DMA_channelA();
	
}

static void upp_program_DMA_recv_channel( void )
{
   // wait for dma active/pending bits to clear
   while ( ioread32( upp_base + UPIS2 ) & 0x00000002 )
        ;

   //temporarily disable interrupts...
   iowrite32( UPIES_MASK, upp_base + UPIEC );

   //------------------------------------------------------------------------
   // Channel A (Rx), (DMA Channel I) //27th November 2014 - 11:11 am
   //------------------------------------------------------------------------
   iowrite32( rxBuf, upp_base + UPID0);  
   iowrite32( ( UPP_RX_LINE_COUNT << 16 | UPP_RX_LINE_SIZE ), upp_base + UPID1);
   iowrite32( UPP_RX_LINE_OFFSET, upp_base + UPID2);

   //And re-enable...
   iowrite32( UPIES_MASK, upp_base + UPIES );
}


#if 0
   //SPRUGJ5B.PDF, Section 2.6.1.9
static void upp_program_DMA_channels( void )
{
   upp_program_DMA_recv_channel( );
   printk(KERN_INFO "upp_program_DMA_channels....OK\n");
}
#endif


//------------------------------------------------------------------------
// User-mode functions read/write/open/close, etc.
//------------------------------------------------------------------------


int upp_open( struct inode *iPtr, struct file *fPtr )
{
   int minor,major;
	
   	read_pending  = 0;
   	//write_pending = 0; 
   	
	minor=iminor(iPtr);
	major=imajor(iPtr);
	printk( KERN_INFO "upp_open: MAJOR(%d), MINOR(%d)\n", major, minor);
	
	//upp_interrupt_clear();
	
	upp_disable();
	upp_program_DMA_channels();
	upp_enable();
	//mdelay(5000);
	//upp_interrupt_clear();
	//upp_interrupt_set();
	//upp_interrupt_enable();
	
	//upp_interrupt_reset();
	//dump_Channel_regs();
	
	//upp_program_DMA_recv_channel();
	//upp_program_DMA_xmit_channel();
	
	//dump_Channel_regs();
	
	return 0;
}

////////////////////////////////////////////////////////////READ FUNCTION STARTS HERE!!!!!!/////////////////////////////////////////
ssize_t upp_read( struct file *fPtr, char __user *buffer, size_t size, loff_t *offset )
{
   int retVal;
   int bytesRead = 0;
   void *bPtr = (void *)buffer;
   
   printk( KERN_INFO "upp_read:before-offset: I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
   
   
   
   printk( KERN_INFO "upp_read:after-offset : I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
   
   

   printk(KERN_INFO "upp_read:START(%d)\n", size);

   if (!bPtr) 
   {
      printk(KERN_INFO "upp_read:ERROR: bPtr is null. Bailing....\n");
      return -1;
   }

   if (size < 1) 
   {
      printk(KERN_INFO "upp_read:Invalid size argument....\n");
      return 0;
   }
//////////////////////////////////////////////modified by Shehryar , 27th Nov 2014 ; 11:51 a.m/////////////////////////////////////////
	upp_program_DMA_channelA();	
	upp_program_DMA_recv_channel();
	setpin_GPIO(); // Toggle GP6P6
	clrpin_GPIO(); 
	
///////////////////////////////////////////// End, 27th Nov 2014 ; 11:56  am ///////////////////////////////////////////////
   //"read_pending" will be set to UPP_RX_LINE_SIZE 
   //(via EOLQ in the ISR handler) or (via EOWQ in the ISR handler)
   printk(KERN_INFO "upp_read: before_event: read_pending[%d]\n" , read_pending );
   wait_event_interruptible( read_queue, read_pending > 0 );
   printk(KERN_INFO "upp_read: after_event:  read_pending[%d]\n" , read_pending );
   
   while (size > read_pending) 
   {
	   printk(KERN_INFO "upp_read loop: bPtr[0x%08X] rxBuf[0x%08X] read_pending[%d]\n" , bPtr, rxBuf, read_pending );
	  retVal = copy_to_user(bPtr, rxBuf, read_pending ); 
      if(retVal)
      {
           printk(KERN_WARNING "upp_read: copy_to_user FAILED.\n");
           return -1;
      }
      read_pending = 0;
      size        -= read_pending; 
      bytesRead   += read_pending;
      bPtr        += read_pending;

      wait_event_interruptible( read_queue, read_pending > 0 );
   }
	
	printk( KERN_INFO "I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
	
	int i=0;
	char *rx_char_buffer = (char*)rxBuf;
	char rxTestBuffer[4097] = {0,};
	for( i=0 ; i<4096 ; i++ )
		rxTestBuffer[i] =  rx_char_buffer[i]?rx_char_buffer[i]:'.' ;
	rxTestBuffer[4096] = '\0';
	char *rxTestBufferSegment[129] = {'\0',};
	printk( KERN_INFO "============================= KERNEL-READ-BUFFER =============================\n" );
	int ofst = 0;
	for( ofst=0 ; ofst<4096 ; ofst+=128 )
	{
		snprintf( rxTestBufferSegment , 129 , "%s" , rxTestBuffer+ofst );
		printk( KERN_INFO "%04d %s" , ofst , rxTestBufferSegment );
	}
	//printk( KERN_INFO "KERNEL-READ-BUFFER [%s]\n" , rxTestBuffer );
	printk( KERN_INFO "I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
	

	printk(KERN_INFO "upp_read: bPtr[0x%08.8x] rxBuf[0x%08.8x] read_pending[%d]\n" , bPtr, rxBuf, read_pending );
	retVal = copy_to_user(bPtr, rxBuf, size ); 
   if(retVal)
   {
        printk(KERN_WARNING "upp_read: copy_to_user FAILED.\n");
        return -1;
   }
   read_pending = 0;
   bytesRead   += size;
   
   printk(KERN_INFO "upp_read....OK\n");
   return bytesRead;
}

//////////////////////////////////////////////READ() FUNCTION ENDS HERE!!!!!!/////////////////////////////////////////////////
//////////////////////////////////////////////USELESS WRITE FUNCTION STARTS BELOW!!!//////////////////////////////////////////
/* ssize_t upp_write( struct file *fPtr, const char __user *buffer, size_t size, loff_t *offset )
{
   int retVal;
   int bytesWritten = 0;
   void *bPtr = (void *)buffer;
  
   //
   ///upp_program_DMA_channelA();
///   upp_disable();
///   upp_program_DMA_channels();
//   upp_enable();
   
   printk( KERN_INFO "I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
   
   printk(KERN_INFO "upp_write:START(%d)\n", size);

   if (!bPtr) 
   {
      printk(KERN_INFO "upp_write:ERROR: bPtr is null. Bailing....\n");
      return -1;
   }

  


   //retVal is ZERO on success...
   retVal = copy_from_user(txBuf, bPtr, size);
   if(retVal)
   {
        printk(KERN_WARNING "upp_write: copy_from_user FAILED.\n");
        return -1;
   }
   wait_event_interruptible( write_queue, write_pending > 0 );
   write_pending = 0;
   bytesWritten += size;


//   upp_disable();
///   upp_program_DMA_channels();
///   upp_enable();
   
   iowrite32( UPP_TX_LINE_OFFSET, upp_base + UPID2);

   
   printk(KERN_INFO "upp_write:rxBuf (%s)\n", (char *)rxBuf);
   printk(KERN_INFO "upp_write:bPtr (%s)\n", (char *)bPtr);

   printk(KERN_INFO "upp_write....OK(%d)\n", bytesWritten);
   printk( KERN_INFO "I-CHANNEL-REGISTERS   UPID0[0x%08X] UPID1[0x%08X] UPID2[0x%08X] UPIS0[0x%08X] UPIS1[0x%08X] UPIS2[0x%08X]\n" , ioread32(upp_base+UPID0) , ioread32(upp_base+UPID1) , ioread32(upp_base+UPID2) , ioread32(upp_base+UPIS0) , ioread32(upp_base+UPIS1) , ioread32(upp_base+UPIS2)  );
  
   return bytesWritten;
} */
////////////////////////////////////////USELESS WRITE FUNCTION ENDS ABOVE!!!!!///////////////////////////////////////////////////////////////////////////////
int upp_release( struct inode *iPtr, struct file *fPtr )
{

   return 0;
}


static struct cdev *UPP_cdev;
static dev_t UPP_MajorMinorNumbers;

struct file_operations upp_fops = { 
  .owner    = THIS_MODULE,
  //.llseek   = no_llseek,
  //.poll     = upp_poll,
  .read     = upp_read,
  //.write    = upp_write,
  //.ioctl	   = upp_ioctl,
  .open     = upp_open,
  //.release  = upp_release,
};


/*
 *  Return ZERO on success.
 *  
 */
static int __init upp_init(void)
{
   int retVal;


   //SPRUGJ5B.PDF, Section 2.6.1.8
   // I'm doing this out-of-order...If the mem-allocation fails,
   // there is no sense in doing anything else, except to bail early... 
   if (upp_mem_alloc() == false)
   {
      printk(KERN_INFO "******ERROR: Could not allocate buffers. Bailing!******\n");
      return -1;
   }

   //SPRUGJ5B.PDF, Section 2.6.1.1
   upp_pin_mux_init();

   //SPRUGJ5B.PDF, Section 2.6.1.2.
   upp_power_and_clocks();

   //SPRUGJ5B.PDF, Section 2.6.1.3, 2.6.1.4
   upp_swrst();

   //SPRUGJ5B.PDF, Section 2.6.1.5
   upp_config();

   //SPRUGJ5B.PDF, Section 2.6.1.6
   upp_interrupt_enable();

   //SPRUGJ5B.PDF, Section 2.6.1.7
   upp_enable();

   //SPRUGJ5B.PDF, Section 2.6.1.9
   //upp_program_DMA_channels();
   //upp_program_DMA_recv_channel();
   //upp_program_DMA_xmit_channel();
   ////////////////////////27th November 2014 ; 11:34 am/////////////////////////////////////////////////////////
   Config_GPIO(); 
   
   //
   //
   //At this point, everything should be properly initialized;
   //Now, I need to allow
   //character-device access: "open()", "read()", "write()", etc.
   //
   //

   UPP_MajorMinorNumbers = MKDEV( 0, 0);
   if ( (retVal = alloc_chrdev_region( &UPP_MajorMinorNumbers, 0, 1, "UPP" )) < 0)
   {
      printk(KERN_INFO "ERROR: Major/Minor number allocation failed.\n");
      return retVal;
   }


   UPP_cdev        = cdev_alloc();
   UPP_cdev->ops   = &upp_fops; 
   UPP_cdev->owner = THIS_MODULE;
   
   if (cdev_add( UPP_cdev, UPP_MajorMinorNumbers, 1) != 0) 
   {
      printk(KERN_INFO "ERROR: UPP driver NOT loaded. CDEV registration failed.\n");
   }
   else
   {
      printk(KERN_INFO "\nUPP Major:%d, Minor:%d\n", MAJOR(UPP_MajorMinorNumbers), MINOR(UPP_MajorMinorNumbers));
   }
   
   printk("UPP driver (1.7.8) succesfully installed.\n Previous stable release was 1.7.7.\n Updated on 27th November 2014 by Shehryar @ TeReSol Pvt Ltd. \n \n "); 
   
   return 0;
}

/*
 * 
 *  
 *  
 */
static void __exit upp_exit(void)
{
   uint32_t regVal;

   // SPRUH77A.PDF, Section 33.2.7.1.1, Table 33-12.
   // clear EN bit of UPPCR to disable the UPP. 
   regVal = ioread32( upp_base + UPPCR );
   regVal &= 0xfffffff7;
   iowrite32( regVal, upp_base + UPPCR );


   free_irq( UPP_INTERRUPT, 0);

 
   if (rxBuf) 
   {
      kfree( rxBuf );
      rxBuf = 0;
   }

   cdev_del( UPP_cdev );
   unregister_chrdev_region( UPP_MajorMinorNumbers, 1);

   printk(KERN_INFO "UPP driver unloaded.\n");
}


MODULE_AUTHOR("Ali Shehryar");
MODULE_DESCRIPTION("OMAP-L138 / AM-1808 UPP bus driver");
MODULE_LICENSE("GPL");
module_init(upp_init);
module_exit(upp_exit);