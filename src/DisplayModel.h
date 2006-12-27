#ifndef _DISPLAY_MODEL_H_
#define _DISPLAY_MODEL_H_

class DisplayModel
{
public:
    virtual ~DisplayModel();

    /* number of pages in PDF document */
    int  pageCount() const {
        return _pageCount;
    }

    void setPageCount(int pageCount) {
        _pageCount = pageCount;
    }

    bool validPageNo(int pageNo) const
    {
        if ((pageNo >= 1) && (pageNo <= pageCount()))
            return true;
        return false;
    }

protected:
    int _pageCount;
};

#endif
