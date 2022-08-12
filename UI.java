
import javax.swing.*;
import java.awt.*;
import java.awt.event.ActionListener;
import java.awt.event.ItemListener;
import java.awt.event.ItemEvent;
import java.awt.event.ActionEvent;
import java.awt.image.BufferedImage;

import javax.swing.event.ChangeEvent;
import javax.swing.event.ChangeListener;


import java.io.File;
import java.io.FileOutputStream;

import java.util.Timer;
import java.util.TimerTask;

public class UI extends JFrame {

    BufferedImage img;
    public TimerTask timerTask;
    int h,w;

    private Boolean record=false;
    int recordings = 0;
    UVC camera;

    void startRecord()
    {
        synchronized(record) {
            record = true;
        }
    }

    public UI(String path,int width,int height,boolean compressed,int bufN,int bufS) throws Exception
    {
        super("Hello World");
        h=height;
        w=width;
        img = new BufferedImage(w,h,BufferedImage.TYPE_INT_RGB);
        setMinimumSize(new Dimension(800,600));
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        JButton a = new JButton("Record");
        a.addActionListener(new ActionListener() {
                @Override
                    public void actionPerformed(ActionEvent e) {
                        startRecord();
                    }
        });
        getContentPane().setLayout(new FlowLayout());
        getContentPane().add(a);
        // center the JLabel
        //JLabel lblText = new JLabel("Hello World!", SwingConstants.CENTER);
        JLabel lblText = new JLabel(new ImageIcon(img));
        // add JLabel to JFrame

        camera = new UVC();
        int[] sizes = camera.open(path);
        for(int i=0;i<sizes.length;i+=2) {
            System.out.println("Supported size " + sizes[i] + "x" + sizes[i+1]);
        }
        UVC.UVCLimits limits = camera.getLimits(); 
        JCheckBox autoBox = new JCheckBox("Auto Exposure/WB");
        autoBox.addItemListener(new ItemListener() {    
                public void itemStateChanged(ItemEvent e) {                 
                    try {
                        camera.setAuto(e.getStateChange()==1);    
                    }
                    catch(Exception ex)
                    {
                        System.out.println(ex.toString());
                    }
                }    
        });
        System.out.println(String.format("min=%f max=%f",limits.exp_msec_min,limits.exp_msec_max));
        JSlider exposure = new JSlider(
            (int)Math.max(1,limits.exp_msec_min),
            (int)limits.exp_msec_max);
        exposure.addChangeListener(new   ChangeListener() {
                  public void stateChanged(ChangeEvent event) {
                    try {
                      camera.setExposure(exposure.getValue());
                    }
                    catch(Exception e)
                    {
                        System.out.println(e.toString());
                    }
                  }
        });
        JSlider gain = new JSlider(0,100);
        gain.addChangeListener(new   ChangeListener() {
                  public void stateChanged(ChangeEvent event) {
                    try {
                      camera.setGain(gain.getValue()/100.0);
                    }
                    catch(Exception e)
                    {
                        System.out.println(e.toString());
                    }
                  }
        });
        JSlider wb = new JSlider(limits.wb_temp_min,limits.wb_temp_max);
        wb.addChangeListener(new   ChangeListener() {
                  public void stateChanged(ChangeEvent event) {
                    try {
                      camera.setWBTemperature(wb.getValue());
                    }
                    catch(Exception e)
                    {
                        System.out.println(e.toString());
                    }
                  }
        });
        getContentPane().add(autoBox);
        getContentPane().add(new JLabel("Exp"));
        getContentPane().add(exposure);
        getContentPane().add(new JLabel("Gain"));
        getContentPane().add(gain);
        getContentPane().add(new JLabel("WB"));
        getContentPane().add(wb);
        

        camera.setFormat(width,height,compressed);
        camera.setBuffers(bufN,bufS);
        /*
        camera.stream(640,480,new UVC.FrameCallback() {
                public void frame(int frame,byte[] data,int w,int h)
                {
                    updateImage(data,w,h);
                }
                public void error(int frame,String message)
                {
                    System.out.println("Got error " + frame + ": " +message);
                }
        });
        */
        camera.stream();
        getContentPane().add(lblText);
        Thread t = new Thread() {
            byte []data = new byte[w*h*3];
            public void run(){
                int counter = 0;
                int frames_to_save = 0;
                while(true) {
                    try {
                        int r = camera.getFrame(1000000,w,h,data);
                        if(r == 0)
                            System.out.println("Timeout");
                        else {
                            System.out.println("Frame:" + r);
                            updateImage(data,w,h);
                        }
                        synchronized(record) {
                            if(record) {
                                recordings++;
                                frames_to_save = 30*10;
                                counter = 0;
                                record=false;
                            }
                        }
                        if(frames_to_save > 0) {
                            frames_to_save --;
                            counter++;
                            //double ts = (System.currentTimeMillis() % (1000L * 3600 * 24))/ 1000.0;
                            //File outputFile = new File("/tmp/seq_" + recordings + "_n_" + counter + "_" + ts + ".ppm");
                            File outputFile = new File("/tmp/seq_" + recordings + "_n_" + counter + ".ppm");
                            try (FileOutputStream outputStream = new FileOutputStream(outputFile)) {
                                outputStream.write(("P6\n" + w + " " + h + " 255\n").getBytes());
                                outputStream.write(data);
                            }
                        }
                    }
                    catch(Exception e) {
                        System.out.println("Error:" + e);
                    }
                }
            }
        };
        t.start();
    }

    void updateImage(byte[] pixels,int w,int h)
    {
        int pos=0;
        for(int r=0;r<h;r++) {
            for(int c=0;c<w;c++) {
                int color = ((int)pixels[pos] & 0xFF) << 16;
                color |= ((int)pixels[pos+1] & 0xFF) << 8;
                color |= ((int)pixels[pos+2] & 0xFF);
                pos+=3;
                img.setRGB(c,r,color);
            }
        }
        repaint();
    }

    public static void main(String[] args) {
        try {
            UI frame = new UI(  args[0],
                                Integer.parseInt(args[1]),
                                Integer.parseInt(args[2]),
                                (Integer.parseInt(args[3]) != 0),
                                Integer.parseInt(args[4]),
                                Integer.parseInt(args[5])
                                );


            // display it
            frame.pack();
            frame.setVisible(true);
        }
        catch(Exception e) {
            System.out.println(e);
        }

    }
}
