#ifndef pages_h
#define pages_h

#include <list>

class Widget;

class Page {
  protected:
    std::list<Widget*> _widgets;
    std::list<Page*> _pages;
    bool _active = false;
    bool _ownsWidgets;
  public:
    explicit Page(bool ownsWidgets = true) : _ownsWidgets(ownsWidgets) {}
    ~Page();
    void loop();
    Widget& addWidget(Widget* widget);
    bool removeWidget(Widget* widget);
    Page& addPage(Page* page);
    bool removePage(Page* page);
    void setActive(bool act);
    bool isActive();
};

class Pager{
  public:
    ~Pager();
    void begin();
    void loop();
    Page& addPage(Page* page, bool setNow = false);
    Page& addOwnedPage(Page* page, bool setNow = false);
    bool removePage(Page* page);
    void setPage(Page* page, bool black=false);
  private:
    std::list<Page*> _pages;
    std::list<Page*> _ownedPages;
    
};


#endif
