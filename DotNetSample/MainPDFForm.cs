using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace PDFViewer
{
    public partial class MainPDFForm : Form
    {
        public MainPDFForm()
        {
            InitializeComponent();
        }

        public void LoadDocument(string pfdFileName)
        {
            userControlPDFViewer.LoadFile(pfdFileName);
        }
    }
}