/* FERT's model (front/fert.c): every control template and screen of FEData\Controls registered, every
   layout of FEData\Layouts loaded into a window, each of its sections made into a control of its
   template; a few templates and a layout checked against their files. Skips (exit 0) without data. */
#include "exe.h"
#include "front/fert.h"
#include "front/fert_expr.h"
#include "vfs.h"
#include "vfs_host.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static char names[256][260];
static int nnames;

static void on_layout(const char *name, bool is_dir, void *user)
{
    (void)user;
    size_t l = strlen(name);
    if (!is_dir && l > 4 && !strcasecmp(name + l - 4, ".lay") && nnames < 256) snprintf(names[nnames++], 260, "%s", name);
}

int main(void)
{
    if (!vfs_mount_default() || !vfs_exists("Porsche.exe")) { printf("fert_test: no game data, skipped\n"); return 0; }
    char err[256];
    if (!exe_init(err, sizeof err)) { printf("FAIL %s\n", err); return 1; }
    int n = Fert_ListControls("FEData\\Controls");
    printf("  %d templates: %d controls, %d screens\n", n, Fert_ControlCount(), Fert_ScreenCount());
    CHECK(Fert_ControlCount() > 60 && Fert_ScreenCount() >= 1);

    /* Label.ctr: two parts, two states, label a LOCALIZEDTEXT with its font, data's typed attributes. */
    FertControl *c = Fert_MakeControl("REAL.LABEL");
    CHECK(c && c->nparts == 2 && c->nstates == 2);
    if (c) FertControl_Update(c);
    if (c && c->nparts == 2) {
        FertPart *label = c->parts[FertControl_findPart(c, "label")], *data = c->parts[FertControl_findPart(c, "data")];
        CHECK(label->kind == FPART_LOCALIZEDTEXT);
        CHECK(!strcmp(FertAttrList_find(&label->attrs, "fontPathName")->value, "univers8b.ffn"));
        CHECK(FertAttr_color(FertAttrList_find(&label->attrs, "foreColor")) == 0xffffffffu);
        CHECK(data->kind == FPART_DATA && data->attrs.n == 4);
        FertAttr *ns = FertAttrList_find(&data->attrs, "nosize");
        CHECK(ns && ns->type == FATTR_INT && FertAttr_int(ns) == 1 && !ns->exposed);
        CHECK(FertAttrList_find(&data->attrs, "transon")->exposed);
        CHECK(!FertAttrList_find(&c->attrs, "size")->exposed);
        int h = FertControl_findState(c, "hidden");
        CHECK(h >= 0 && c->states[h].nparts == 1);
    }
    FertControl_free(c);

    /* Expressions: symbols expanded as text (no parentheses added), C operators, errors 0. */
    FertSyms_Clear();
    FertSyms_Define("data.align", "1");
    FertSyms_Define("label.size.width", "40");
    FertSyms_Define("a.b", "1+2");
    FertSyms_Define("loop", "loop+1");
    CHECK(FertExpr_Eval("(0-label.size.width)/2*data.align") == -20);
    CHECK(FertExpr_Eval("a.b*3") == 7);
    CHECK(FertExpr_Eval("(data.align&1)?0x30ffffff:0") == 0x30ffffff);
    /* The original's interpreter: names it doesn't know are 0, division by 0 keeps the left side, an
       empty operand is 0; an operator where none can be (',') or unmatched brackets give 0. */
    CHECK(FertExpr_Eval("unknown.name+1") == 1);
    CHECK(FertExpr_Eval("loop") == 0);
    CHECK(FertExpr_Eval("7/0") == 7 && FertExpr_Eval("0 ? 1/0 : 5") == 5);
    CHECK(FertExpr_Eval("-(8/2),") == 0 && FertExpr_Eval("(1+2") == 0);
    CHECK(FertExpr_Eval("(0&1)?330-(0+48+):330") == 330 && FertExpr_Eval("(0+48+)") == 48);
    CHECK(FertExpr_Eval("1=1") == 1 && FertExpr_Eval("2#3") == 1 && FertExpr_Eval("$ff+%101") == 260);
    CHECK(FertExpr_Eval("abs(0-5)*2") == 10 && FertExpr_Eval("<0x12345678") == 0x1234);
    FertSyms_Clear();

    /* bottombar.lay: its screen, then the controls with their layout values. */
    FertControl *w = FertControl_new(true);
    CHECK(FertWindow_LoadLayout(w, "FEData\\Layouts\\bottombar.lay"));
    CHECK(w->screen != NULL);
    FertControl *lab = NULL;
    for (int i = 0; i < w->nchildren; i++)
        if (!strcmp(w->children[i]->name, "ID_ACCOUNTTHINGY")) lab = w->children[i];
    CHECK(lab && !strcmp(lab->id, "REAL.LABEL"));
    if (lab) {
        int32_t p[2];
        FertAttr_point(FertAttrList_find(&lab->attrs, "pos"), p);
        CHECK(p[0] == 191 && p[1] == 464);
        FertPart *l = lab->parts[FertControl_findPart(lab, "label")];
        CHECK(!strcmp(FertAttr_str(FertAttrList_find(&l->attrs, "fontPathName")), "univers7b.ffn"));
        CHECK(!strcmp(FertAttr_str(FertAttrList_find(&l->attrs, "stringID")), "10504"));
        CHECK(FertAttr_int(FertAttrList_find(&lab->parts[FertControl_findPart(lab, "data")]->attrs, "align")) == 2);
    }
    FertControl_free(w);

    {   /* QuickRace.lay: the combo boxes take their accessors from the layout */
        FertControl *q = FertControl_new(true);
        CHECK(FertWindow_LoadLayout(q, "FEData\\Layouts\\QuickRace.lay"));
        int found = 0;
        for (int i = 0; i < q->nchildren; i++) {
            FertControl *c = q->children[i];
            for (int k = 0; k < c->nparts; k++) {
                FertAttr *a = FertAttrList_find(&c->parts[k]->attrs, "accessor");
                if (a && strcmp(c->id, "NFS5.ComboBoxSlider") == 0) {
                    printf("  combo %d accessor '%s' exposed %d\n", i, a->value, a->exposed);
                    if (strcmp(a->value, "Put_Accessor_Value_Here")) found++;
                }
            }
        }
        CHECK(found == 4);   /* (empty values once took the next line: a section loaded twice) */
        FertControl_free(q);
    }

    /* Every layout. */
    vfs_list("FEData\\Layouts", on_layout, NULL);
    int sections = 0, made = 0, unknown = 0;
    for (int i = 0; i < nnames; i++) {
        char path[300];
        snprintf(path, sizeof path, "FEData\\Layouts\\%s", names[i]);
        FertControl *lw = FertControl_new(true);
        bool ok = FertWindow_LoadLayout(lw, path);
        CHECK(ok);
        if (!lw->screen) { printf("  %s: no screen\n", names[i]); unknown++; }
        made += lw->nchildren;
        FertControl_free(lw);
        (void)sections;
    }
    printf("  %d layouts, %d controls placed, %d without a screen\n", nnames, made, unknown);
    CHECK(nnames > 90 && made > 500);
    Fert_FreeTemplates();
    printf("fert_test: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
