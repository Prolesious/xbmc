/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "GUIDialogVideoInfo.h"
#include "Application.h"
#include "guilib/GUIWindow.h"
#include "Util.h"
#include "guilib/GUIImage.h"
#include "utils/StringUtils.h"
#include "utils/URIUtils.h"
#include "video/windows/GUIWindowVideoNav.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "video/VideoInfoScanner.h"
#include "ApplicationMessenger.h"
#include "video/VideoInfoTag.h"
#include "guilib/GUIKeyboardFactory.h"
#include "guilib/GUIWindowManager.h"
#include "dialogs/GUIDialogOK.h"
#include "dialogs/GUIDialogYesNo.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogProgress.h"
#include "filesystem/File.h"
#include "FileItem.h"
#include "storage/MediaManager.h"
#include "utils/AsyncFileCopy.h"
#include "profiles/ProfilesManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/MediaSourceSettings.h"
#include "settings/Settings.h"
#include "guilib/Key.h"
#include "guilib/LocalizeStrings.h"
#include "GUIUserMessages.h"
#include "TextureCache.h"
#include "music/MusicDatabase.h"
#include "URL.h"
#include "video/VideoThumbLoader.h"
#include "filesystem/Directory.h"
#include "filesystem/VideoDatabaseDirectory.h"
#ifdef HAS_UPNP
#include "network/upnp/UPnP.h"
#endif

using namespace std;
using namespace XFILE;

#define CONTROL_IMAGE                3
#define CONTROL_TEXTAREA             4
#define CONTROL_BTN_TRACKS           5
#define CONTROL_BTN_REFRESH          6
#define CONTROL_BTN_PLAY             8
#define CONTROL_BTN_RESUME           9
#define CONTROL_BTN_GET_THUMB       10
#define CONTROL_BTN_PLAY_TRAILER    11
#define CONTROL_BTN_GET_FANART      12
#define CONTROL_BTN_DIRECTOR        13

#define CONTROL_LIST                50

CGUIDialogVideoInfo::CGUIDialogVideoInfo(void)
    : CGUIDialog(WINDOW_DIALOG_VIDEO_INFO, "DialogVideoInfo.xml")
    , m_movieItem(new CFileItem)
{
  m_bRefreshAll = true;
  m_bRefresh = false;
  m_hasUpdatedThumb = false;
  m_castList = new CFileItemList;
  m_loadType = KEEP_IN_MEMORY;
}

CGUIDialogVideoInfo::~CGUIDialogVideoInfo(void)
{
  delete m_castList;
}

bool CGUIDialogVideoInfo::OnMessage(CGUIMessage& message)
{
  switch ( message.GetMessage() )
  {
  case GUI_MSG_WINDOW_DEINIT:
    {
      ClearCastList();
    }
    break;

  case GUI_MSG_CLICKED:
    {
      int iControl = message.GetSenderId();
      if (iControl == CONTROL_BTN_REFRESH)
      {
        if (m_movieItem->GetVideoInfoTag()->m_iSeason < 0 && !m_movieItem->GetVideoInfoTag()->m_strShowTitle.IsEmpty()) // tv show
        {
          bool bCanceled=false;
          if (CGUIDialogYesNo::ShowAndGetInput(20377,20378,-1,-1,bCanceled))
          {
            m_bRefreshAll = true;
            CVideoDatabase db;
            if (db.Open())
            {
              db.SetPathHash(m_movieItem->GetVideoInfoTag()->m_strPath,"");
              db.Close();
            }
          }
          else
            m_bRefreshAll = false;

          if (bCanceled)
            return false;
        }
        m_bRefresh = true;
        Close();
        return true;
      }
      else if (iControl == CONTROL_BTN_TRACKS)
      {
        m_bViewReview = !m_bViewReview;
        Update();
      }
      else if (iControl == CONTROL_BTN_PLAY)
      {
        Play();
      }
      else if (iControl == CONTROL_BTN_RESUME)
      {
        Play(true);
      }
      else if (iControl == CONTROL_BTN_GET_THUMB)
      {
        OnGetArt();
      }
      else if (iControl == CONTROL_BTN_PLAY_TRAILER)
      {
        PlayTrailer();
      }
      else if (iControl == CONTROL_BTN_GET_FANART)
      {
        OnGetFanart();
      }
      else if (iControl == CONTROL_BTN_DIRECTOR)
      {
        CStdString strDirector = StringUtils::Join(m_movieItem->GetVideoInfoTag()->m_director, g_advancedSettings.m_videoItemSeparator);
        OnSearch(strDirector);
      }
      else if (iControl == CONTROL_LIST)
      {
        int iAction = message.GetParam1();
        if (ACTION_SELECT_ITEM == iAction || ACTION_MOUSE_LEFT_CLICK == iAction)
        {
          CGUIMessage msg(GUI_MSG_ITEM_SELECTED, GetID(), iControl);
          OnMessage(msg);
          int iItem = msg.GetParam1();
          if (iItem < 0 || iItem >= m_castList->Size())
            break;
          CStdString strItem = m_castList->Get(iItem)->GetLabel();
          CStdString strFind;
          strFind.Format(" %s ",g_localizeStrings.Get(20347));
          int iPos = strItem.Find(strFind);
          if (iPos == -1)
            iPos = strItem.size();
          CStdString tmp = strItem.Left(iPos);
          OnSearch(tmp);
        }
      }
    }
    break;
  case GUI_MSG_NOTIFY_ALL:
    {
      if (IsActive() && message.GetParam1() == GUI_MSG_UPDATE_ITEM && message.GetItem())
      {
        CFileItemPtr item = boost::static_pointer_cast<CFileItem>(message.GetItem());
        if (item && m_movieItem->GetPath().Equals(item->GetPath()))
        { // Just copy over the stream details and the thumb if we don't already have one
          if (!m_movieItem->HasArt("thumb"))
            m_movieItem->SetArt("thumb", item->GetArt("thumb"));
          m_movieItem->GetVideoInfoTag()->m_streamDetails = item->GetVideoInfoTag()->m_streamDetails;
        }
        return true;
      }
    }
  }

  return CGUIDialog::OnMessage(message);
}

void CGUIDialogVideoInfo::OnInitWindow()
{
  m_bRefresh = false;
  m_bRefreshAll = true;
  m_hasUpdatedThumb = false;
  m_bViewReview = true;

  CVideoDatabase database;
  ADDON::ScraperPtr scraper;

  if(database.Open())
  {
    scraper = database.GetScraperForPath(m_movieItem->GetVideoInfoTag()->GetPath());
    database.Close();
  }

  CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_REFRESH, (CProfilesManager::Get().GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Left(2).Equals("xx") && scraper);
  CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_GET_THUMB, (CProfilesManager::Get().GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Mid(2).Equals("plugin"));

  VIDEODB_CONTENT_TYPE type = (VIDEODB_CONTENT_TYPE)m_movieItem->GetVideoContentType();
  if (type == VIDEODB_CONTENT_TVSHOWS || type == VIDEODB_CONTENT_MOVIES)
    CONTROL_ENABLE_ON_CONDITION(CONTROL_BTN_GET_FANART, (CProfilesManager::Get().GetCurrentProfile().canWriteDatabases() || g_passwordManager.bMasterUser) && !m_movieItem->GetVideoInfoTag()->m_strIMDBNumber.Mid(2).Equals("plugin"));
  else
    CONTROL_DISABLE(CONTROL_BTN_GET_FANART);

  Update();

  CGUIDialog::OnInitWindow();
}

bool CGUIDialogVideoInfo::OnAction(const CAction &action)
{
  if (action.GetID() == ACTION_SHOW_INFO)
  {
    Close();
    return true;
  }
  return CGUIDialog::OnAction(action);
}

void CGUIDialogVideoInfo::SetMovie(const CFileItem *item)
{
  *m_movieItem = *item;
  // setup cast list + determine type.  We need to do this here as it makes
  // sure that content type (among other things) is set correctly for the
  // old fixed id labels that we have floating around (they may be using
  // content type to determine visibility, so we'll set the wrong label)
  ClearCastList();
  VIDEODB_CONTENT_TYPE type = (VIDEODB_CONTENT_TYPE)m_movieItem->GetVideoContentType();
  if (type == VIDEODB_CONTENT_MUSICVIDEOS)
  { // music video
    CMusicDatabase database;
    database.Open();
    const std::vector<std::string> &artists = m_movieItem->GetVideoInfoTag()->m_artist;
    for (std::vector<std::string>::const_iterator it = artists.begin(); it != artists.end(); ++it)
    {
      int idArtist = database.GetArtistByName(*it);
      CStdString thumb = database.GetArtForItem(idArtist, "artist", "thumb");
      CFileItemPtr item(new CFileItem(*it));
      if (!thumb.empty())
        item->SetArt("thumb", thumb);
      item->SetIconImage("DefaultArtist.png");
      m_castList->Add(item);
    }
    m_castList->SetContent("musicvideos");
  }
  else
  { // movie/show/episode
    for (CVideoInfoTag::iCast it = m_movieItem->GetVideoInfoTag()->m_cast.begin(); it != m_movieItem->GetVideoInfoTag()->m_cast.end(); ++it)
    {
      CStdString character;
      if (it->strRole.IsEmpty())
        character = it->strName;
      else
        character.Format("%s %s %s", it->strName.c_str(), g_localizeStrings.Get(20347).c_str(), it->strRole.c_str());
      CFileItemPtr item(new CFileItem(it->strName));
      if (!it->thumb.IsEmpty())
        item->SetArt("thumb", it->thumb);
      else if (CSettings::Get().GetBool("videolibrary.actorthumbs"))
      { // backward compatibility
        CStdString thumb = CScraperUrl::GetThumbURL(it->thumbUrl.GetFirstThumb());
        if (!thumb.IsEmpty())
        {
          item->SetArt("thumb", thumb);
          CTextureCache::Get().BackgroundCacheImage(thumb);
        }
      }
      item->SetIconImage("DefaultActor.png");
      item->SetLabel(character);
      m_castList->Add(item);
    }
    // determine type:
    if (type == VIDEODB_CONTENT_TVSHOWS)
    {
      m_castList->SetContent("tvshows");
      // special case stuff for shows (not currently retrieved from the library in filemode (ref: GetTvShowInfo vs GetTVShowsByWhere)
      m_movieItem->m_dateTime = m_movieItem->GetVideoInfoTag()->m_premiered;
      if(m_movieItem->GetVideoInfoTag()->m_iYear == 0 && m_movieItem->m_dateTime.IsValid())
        m_movieItem->GetVideoInfoTag()->m_iYear = m_movieItem->m_dateTime.GetYear();
      m_movieItem->SetProperty("totalepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode);
      m_movieItem->SetProperty("numepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode); // info view has no concept of current watched/unwatched filter as we could come here from files view, but set for consistency
      m_movieItem->SetProperty("watchedepisodes", m_movieItem->GetVideoInfoTag()->m_playCount);
      m_movieItem->SetProperty("unwatchedepisodes", m_movieItem->GetVideoInfoTag()->m_iEpisode - m_movieItem->GetVideoInfoTag()->m_playCount);
      m_movieItem->GetVideoInfoTag()->m_playCount = (m_movieItem->GetVideoInfoTag()->m_iEpisode == m_movieItem->GetVideoInfoTag()->m_playCount) ? 1 : 0;
    }
    else if (type == VIDEODB_CONTENT_EPISODES)
    {
      m_castList->SetContent("episodes");
      // special case stuff for episodes (not currently retrieved from the library in filemode (ref: GetEpisodeInfo vs GetEpisodesByWhere)
      m_movieItem->m_dateTime = m_movieItem->GetVideoInfoTag()->m_firstAired;
      if(m_movieItem->GetVideoInfoTag()->m_iYear == 0 && m_movieItem->m_dateTime.IsValid())
        m_movieItem->GetVideoInfoTag()->m_iYear = m_movieItem->m_dateTime.GetYear();
      // retrieve the season thumb.
      // TODO: should we use the thumbloader for this?
      CVideoDatabase db;
      if (db.Open())
      {
        if (m_movieItem->GetVideoInfoTag()->m_iSeason > -1)
        {
          int seasonID = m_movieItem->GetVideoInfoTag()->m_iIdSeason;
          if (seasonID < 0)
            seasonID = db.GetSeasonId(m_movieItem->GetVideoInfoTag()->m_iIdShow,
                                      m_movieItem->GetVideoInfoTag()->m_iSeason);
          CGUIListItem::ArtMap thumbs;
          if (db.GetArtForItem(seasonID, "season", thumbs))
          {
            for (CGUIListItem::ArtMap::iterator i = thumbs.begin(); i != thumbs.end(); i++)
              m_movieItem->SetArt("season." + i->first, i->second);
          }
        }
        db.Close();
      }
    }
    else if (type == VIDEODB_CONTENT_MOVIES)
    {
      m_castList->SetContent("movies");

      // local trailers should always override non-local, so check 
      // for a local one if the registered trailer is online
      if (m_movieItem->GetVideoInfoTag()->m_strTrailer.IsEmpty() ||
          URIUtils::IsInternetStream(m_movieItem->GetVideoInfoTag()->m_strTrailer))
      {
        CStdString localTrailer = m_movieItem->FindTrailer();
        if (!localTrailer.IsEmpty())
        {
          m_movieItem->GetVideoInfoTag()->m_strTrailer = localTrailer;
          CVideoDatabase database;
          if(database.Open())
          {
            database.SetSingleValue(VIDEODB_CONTENT_MOVIES, VIDEODB_ID_TRAILER,
                                    m_movieItem->GetVideoInfoTag()->m_iDbId,
                                    m_movieItem->GetVideoInfoTag()->m_strTrailer);
            database.Close();
            CUtil::DeleteVideoDatabaseDirectoryCache();
          }
        }
      }
    }
  }
  CVideoThumbLoader loader;
  loader.LoadItem(m_movieItem.get());
}

void CGUIDialogVideoInfo::Update()
{
  // setup plot text area
  CStdString strTmp = m_movieItem->GetVideoInfoTag()->m_strPlot;
  if (!(!m_movieItem->GetVideoInfoTag()->m_strShowTitle.IsEmpty() && m_movieItem->GetVideoInfoTag()->m_iSeason == 0)) // dont apply to tvshows
    if (m_movieItem->GetVideoInfoTag()->m_playCount == 0 && !CSettings::Get().GetBool("videolibrary.showunwatchedplots"))
      strTmp = g_localizeStrings.Get(20370);

  strTmp.Trim();
  SetLabel(CONTROL_TEXTAREA, strTmp);

  CGUIMessage msg(GUI_MSG_LABEL_BIND, GetID(), CONTROL_LIST, 0, 0, m_castList);
  OnMessage(msg);

  if (GetControl(CONTROL_BTN_TRACKS)) // if no CONTROL_BTN_TRACKS found - allow skinner full visibility control over CONTROL_TEXTAREA and CONTROL_LIST
  {
    if (m_bViewReview)
    {
      if (!m_movieItem->GetVideoInfoTag()->m_artist.empty())
      {
        SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 133);
      }
      else
      {
        SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 206);
      }

      SET_CONTROL_HIDDEN(CONTROL_LIST);
      SET_CONTROL_VISIBLE(CONTROL_TEXTAREA);
    }
    else
    {
      SET_CONTROL_LABEL(CONTROL_BTN_TRACKS, 207);

      SET_CONTROL_HIDDEN(CONTROL_TEXTAREA);
      SET_CONTROL_VISIBLE(CONTROL_LIST);
    }
  }

  // Check for resumability
  if (m_movieItem->GetVideoInfoTag()->m_resumePoint.timeInSeconds > 0.0)
    CONTROL_ENABLE(CONTROL_BTN_RESUME);
  else
    CONTROL_DISABLE(CONTROL_BTN_RESUME);

  CONTROL_ENABLE(CONTROL_BTN_PLAY);

  // update the thumbnail
  const CGUIControl* pControl = GetControl(CONTROL_IMAGE);
  if (pControl)
  {
    CGUIImage* pImageControl = (CGUIImage*)pControl;
    pImageControl->FreeResources();
    pImageControl->SetFileName(m_movieItem->GetArt("thumb"));
  }
  // tell our GUI to completely reload all controls (as some of them
  // are likely to have had this image in use so will need refreshing)
  if (m_hasUpdatedThumb)
  {
    CGUIMessage reload(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_REFRESH_THUMBS);
    g_windowManager.SendMessage(reload);
  }
}

bool CGUIDialogVideoInfo::NeedRefresh() const
{
  return m_bRefresh;
}

bool CGUIDialogVideoInfo::RefreshAll() const
{
  return m_bRefreshAll;
}

/// \brief Search the current directory for a string got from the virtual keyboard
void CGUIDialogVideoInfo::OnSearch(CStdString& strSearch)
{
  CGUIDialogProgress *progress = (CGUIDialogProgress *)g_windowManager.GetWindow(WINDOW_DIALOG_PROGRESS);
  if (progress)
  {
    progress->SetHeading(194);
    progress->SetLine(0, strSearch);
    progress->SetLine(1, "");
    progress->SetLine(2, "");
    progress->StartModal();
    progress->Progress();
  }
  CFileItemList items;
  DoSearch(strSearch, items);

  if (progress)
    progress->Close();

  if (items.Size())
  {
    CGUIDialogSelect* pDlgSelect = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    pDlgSelect->Reset();
    pDlgSelect->SetHeading(283);

    for (int i = 0; i < (int)items.Size(); i++)
    {
      CFileItemPtr pItem = items[i];
      pDlgSelect->Add(pItem->GetLabel());
    }

    pDlgSelect->DoModal();

    int iItem = pDlgSelect->GetSelectedLabel();
    if (iItem < 0)
      return;

    CFileItem* pSelItem = new CFileItem(*items[iItem]);

    OnSearchItemFound(pSelItem);

    delete pSelItem;
  }
  else
  {
    CGUIDialogOK::ShowAndGetInput(194, 284, 0, 0);
  }
}

/// \brief Make the actual search for the OnSearch function.
/// \param strSearch The search string
/// \param items Items Found
void CGUIDialogVideoInfo::DoSearch(CStdString& strSearch, CFileItemList& items)
{
  CVideoDatabase db;
  if (!db.Open())
    return;

  CFileItemList movies;
  db.GetMoviesByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20338) + "] ", items);

  db.GetTvShowsByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strShowTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20364) + "] ", items);

  db.GetEpisodesByActor(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = movies[i]->GetVideoInfoTag()->m_strTitle + " (" +  movies[i]->GetVideoInfoTag()->m_strShowTitle + ")";
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20359) + "] ", items);

  db.GetMusicVideosByArtist(strSearch, movies);
  for (int i = 0; i < movies.Size(); ++i)
  {
    CStdString label = StringUtils::Join(movies[i]->GetVideoInfoTag()->m_artist, g_advancedSettings.m_videoItemSeparator) + " - " + movies[i]->GetVideoInfoTag()->m_strTitle;
    if (movies[i]->GetVideoInfoTag()->m_iYear > 0)
      label.AppendFormat(" (%i)", movies[i]->GetVideoInfoTag()->m_iYear);
    movies[i]->SetLabel(label);
  }
  CGUIWindowVideoBase::AppendAndClearSearchItems(movies, "[" + g_localizeStrings.Get(20391) + "] ", items);
  db.Close();
}

/// \brief React on the selected search item
/// \param pItem Search result item
void CGUIDialogVideoInfo::OnSearchItemFound(const CFileItem* pItem)
{
  VIDEODB_CONTENT_TYPE type = (VIDEODB_CONTENT_TYPE)pItem->GetVideoContentType();

  CVideoDatabase db;
  if (!db.Open())
    return;

  CVideoInfoTag movieDetails;
  if (type == VIDEODB_CONTENT_MOVIES)
    db.GetMovieInfo(pItem->GetPath(), movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_EPISODES)
    db.GetEpisodeInfo(pItem->GetPath(), movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_TVSHOWS)
    db.GetTvShowInfo(pItem->GetPath(), movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  if (type == VIDEODB_CONTENT_MUSICVIDEOS)
    db.GetMusicVideoInfo(pItem->GetPath(), movieDetails, pItem->GetVideoInfoTag()->m_iDbId);
  db.Close();

  CFileItem item(*pItem);
  *item.GetVideoInfoTag() = movieDetails;
  SetMovie(&item);
  // refresh our window entirely
  Close();
  DoModal();
}

void CGUIDialogVideoInfo::ClearCastList()
{
  CGUIMessage msg(GUI_MSG_LABEL_RESET, GetID(), CONTROL_LIST);
  OnMessage(msg);
  m_castList->Clear();
}

void CGUIDialogVideoInfo::Play(bool resume)
{
  if (!m_movieItem->GetVideoInfoTag()->m_strEpisodeGuide.IsEmpty())
  {
    CStdString strPath;
    strPath.Format("videodb://tvshows/titles/%i/",m_movieItem->GetVideoInfoTag()->m_iDbId);
    Close();
    g_windowManager.ActivateWindow(WINDOW_VIDEO_NAV,strPath);
    return;
  }

  CFileItem movie(*m_movieItem->GetVideoInfoTag());
  if (m_movieItem->GetVideoInfoTag()->m_strFileNameAndPath.IsEmpty())
    movie.SetPath(m_movieItem->GetPath());
  CGUIWindowVideoNav* pWindow = (CGUIWindowVideoNav*)g_windowManager.GetWindow(WINDOW_VIDEO_NAV);
  if (pWindow)
  {
    // close our dialog
    Close(true);
    if (resume)
      movie.m_lStartOffset = STARTOFFSET_RESUME;
    else if (!CGUIWindowVideoBase::ShowResumeMenu(movie)) 
    {
      // The Resume dialog was closed without any choice
      DoModal();
      return;
    }
    pWindow->PlayMovie(&movie);
  }
}

string CGUIDialogVideoInfo::ChooseArtType(const CFileItem &videoItem, map<string, string> &currentArt)
{
  // prompt for choice
  CGUIDialogSelect *dialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
  if (!dialog || !videoItem.HasVideoInfoTag())
    return "";

  CFileItemList items;
  dialog->SetHeading(13511);
  dialog->Reset();
  dialog->SetUseDetails(true);
  dialog->EnableButton(true, 13516);

  CVideoDatabase db;
  db.Open();

  vector<string> artTypes = CVideoThumbLoader::GetArtTypes(videoItem.GetVideoInfoTag()->m_type);

  // add in any stored art for this item that is non-empty.
  db.GetArtForItem(videoItem.GetVideoInfoTag()->m_iDbId, videoItem.GetVideoInfoTag()->m_type, currentArt);
  for (CGUIListItem::ArtMap::iterator i = currentArt.begin(); i != currentArt.end(); ++i)
  {
    if (!i->second.empty() && find(artTypes.begin(), artTypes.end(), i->first) == artTypes.end())
      artTypes.push_back(i->first);
  }

  // add any art types that exist for other media items of the same type
  vector<string> dbArtTypes;
  db.GetArtTypes(videoItem.GetVideoInfoTag()->m_type, dbArtTypes);
  for (vector<string>::const_iterator it = dbArtTypes.begin(); it != dbArtTypes.end(); it++)
  {
    if (find(artTypes.begin(), artTypes.end(), *it) == artTypes.end())
      artTypes.push_back(*it);
  }

  for (vector<string>::const_iterator i = artTypes.begin(); i != artTypes.end(); ++i)
  {
    string type = *i;
    CFileItemPtr item(new CFileItem(type, "false"));
    item->SetLabel(type);
    if (videoItem.HasArt(type))
      item->SetArt("thumb", videoItem.GetArt(type));
    items.Add(item);
  }

  dialog->SetItems(&items);
  dialog->DoModal();

  if (dialog->IsButtonPressed())
  {
    // Get the new artwork name
    CStdString strArtworkName;
    if (!CGUIKeyboardFactory::ShowAndGetInput(strArtworkName, g_localizeStrings.Get(13516), false))
      return "";

    return strArtworkName;
  }

  return dialog->GetSelectedItem()->GetLabel();
}

void CGUIDialogVideoInfo::OnGetArt()
{
  map<string, string> currentArt;
  string type = ChooseArtType(*m_movieItem, currentArt);
  if (type.empty())
    return; // cancelled

  // TODO: this can be removed once these are unified.
  if (type == "fanart")
    OnGetFanart();
  else
  {
    CFileItemList items;

    // Current thumb
    if (m_movieItem->HasArt(type))
    {
      CFileItemPtr item(new CFileItem("thumb://Current", false));
      item->SetArt("thumb", m_movieItem->GetArt(type));
      item->SetLabel(g_localizeStrings.Get(13512));
      items.Add(item);
    }
    else if ((type == "poster" || type == "banner") && currentArt.find("thumb") != currentArt.end())
    { // add the 'thumb' type in
      CFileItemPtr item(new CFileItem("thumb://Thumb", false));
      item->SetArt("thumb", currentArt["thumb"]);
      item->SetLabel(g_localizeStrings.Get(13512));
      items.Add(item);
    }

    // Grab the thumbnails from the web
    vector<CStdString> thumbs;
    int season = (m_movieItem->GetVideoInfoTag()->m_type == "season") ? m_movieItem->GetVideoInfoTag()->m_iSeason : -1;
    m_movieItem->GetVideoInfoTag()->m_strPictureURL.GetThumbURLs(thumbs, type, season);

    for (unsigned int i = 0; i < thumbs.size(); ++i)
    {
      CStdString strItemPath;
      strItemPath.Format("thumb://Remote%i", i);
      CFileItemPtr item(new CFileItem(strItemPath, false));
      item->SetArt("thumb", thumbs[i]);
      item->SetIconImage("DefaultPicture.png");
      item->SetLabel(g_localizeStrings.Get(13513));

      // TODO: Do we need to clear the cached image?
      //    CTextureCache::Get().ClearCachedImage(thumb);
      items.Add(item);
    }

    CStdString localThumb = CVideoThumbLoader::GetLocalArt(*m_movieItem, type);
    if (!localThumb.empty())
    {
      CFileItemPtr item(new CFileItem("thumb://Local", false));
      item->SetArt("thumb", localThumb);
      item->SetLabel(g_localizeStrings.Get(13514));
      items.Add(item);
    }
    else
    { // no local thumb exists, so we are just using the IMDb thumb or cached thumb
      // which is probably the IMDb thumb.  These could be wrong, so allow the user
      // to delete the incorrect thumb
      CFileItemPtr item(new CFileItem("thumb://None", false));
      item->SetIconImage("DefaultVideo.png");
      item->SetLabel(g_localizeStrings.Get(13515));
      items.Add(item);
    }

    CStdString result;
    VECSOURCES sources(*CMediaSourceSettings::Get().GetSources("video"));
    AddItemPathToFileBrowserSources(sources, *m_movieItem);
    g_mediaManager.GetLocalDrives(sources);
    if (CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(13511), result) &&
        result != "thumb://Current") // user didn't choose the one they have
    {
      CStdString newThumb;
      if (result.Left(14) == "thumb://Remote")
      {
        int number = atoi(result.Mid(14));
        newThumb = thumbs[number];
      }
      else if (result == "thumb://Thumb")
        newThumb = currentArt["thumb"];
      else if (result == "thumb://Local")
        newThumb = localThumb;
      else if (CFile::Exists(result))
        newThumb = result;
      else // none
        newThumb.clear();

      // update thumb in the database
      CVideoDatabase db;
      if (db.Open())
      {
        db.SetArtForItem(m_movieItem->GetVideoInfoTag()->m_iDbId, m_movieItem->GetVideoInfoTag()->m_type, type, newThumb);
        db.Close();
      }
      CUtil::DeleteVideoDatabaseDirectoryCache(); // to get them new thumbs to show
      m_movieItem->SetArt(type, newThumb);
      if (m_movieItem->HasProperty("set_folder_thumb"))
      { // have a folder thumb to set as well
        VIDEO::CVideoInfoScanner::ApplyThumbToFolder(m_movieItem->GetProperty("set_folder_thumb").asString(), newThumb);
      }
      m_hasUpdatedThumb = true;
    }
  }

  // Update our screen
  Update();

  // re-open the art selection dialog as we come back from
  // the image selection dialog
  OnGetArt();
}

// Allow user to select a Fanart
void CGUIDialogVideoInfo::OnGetFanart()
{
  CFileItemList items;

  CFileItem item(*m_movieItem->GetVideoInfoTag());
  if (item.HasArt("fanart"))
  {
    CFileItemPtr itemCurrent(new CFileItem("fanart://Current",false));
    itemCurrent->SetArt("thumb", item.GetArt("fanart"));
    itemCurrent->SetLabel(g_localizeStrings.Get(20440));
    items.Add(itemCurrent);
  }

  // ensure the fanart is unpacked
  m_movieItem->GetVideoInfoTag()->m_fanart.Unpack();

  // Grab the thumbnails from the web
  for (unsigned int i = 0; i < m_movieItem->GetVideoInfoTag()->m_fanart.GetNumFanarts(); i++)
  {
    CStdString strItemPath;
    strItemPath.Format("fanart://Remote%i",i);
    CFileItemPtr item(new CFileItem(strItemPath, false));
    CStdString thumb = m_movieItem->GetVideoInfoTag()->m_fanart.GetPreviewURL(i);
    item->SetArt("thumb", CTextureCache::GetWrappedThumbURL(thumb));
    item->SetIconImage("DefaultPicture.png");
    item->SetLabel(g_localizeStrings.Get(20441));

    // TODO: Do we need to clear the cached image?
//    CTextureCache::Get().ClearCachedImage(thumb);
    items.Add(item);
  }

  CStdString strLocal = item.GetLocalFanart();
  if (!strLocal.IsEmpty())
  {
    CFileItemPtr itemLocal(new CFileItem("fanart://Local",false));
    itemLocal->SetArt("thumb", strLocal);
    itemLocal->SetLabel(g_localizeStrings.Get(20438));

    // TODO: Do we need to clear the cached image?
    CTextureCache::Get().ClearCachedImage(strLocal);
    items.Add(itemLocal);
  }
  else
  {
    CFileItemPtr itemNone(new CFileItem("fanart://None", false));
    itemNone->SetIconImage("DefaultVideo.png");
    itemNone->SetLabel(g_localizeStrings.Get(20439));
    items.Add(itemNone);
  }

  CStdString result;
  VECSOURCES sources(*CMediaSourceSettings::Get().GetSources("video"));
  AddItemPathToFileBrowserSources(sources, item);
  g_mediaManager.GetLocalDrives(sources);
  bool flip=false;
  if (!CGUIDialogFileBrowser::ShowAndGetImage(items, sources, g_localizeStrings.Get(20437), result, &flip, 20445) || result.Equals("fanart://Current"))
    return;   // user cancelled

  if (result.Equals("fanart://Local"))
    result = strLocal;

  if (result.Left(15) == "fanart://Remote")
  {
    int iFanart = atoi(result.Mid(15).c_str());
    // set new primary fanart, and update our database accordingly
    m_movieItem->GetVideoInfoTag()->m_fanart.SetPrimaryFanart(iFanart);
    CVideoDatabase db;
    if (db.Open())
    {
      db.UpdateFanart(*m_movieItem, (VIDEODB_CONTENT_TYPE)m_movieItem->GetVideoContentType());
      db.Close();
    }
    result = m_movieItem->GetVideoInfoTag()->m_fanart.GetImageURL();
  }
  else if (result.Equals("fanart://None") || !CFile::Exists(result))
    result.clear();

  // set the fanart image
  if (flip && !result.IsEmpty())
    result = CTextureCache::GetWrappedImageURL(result, "", "flipped");
  CVideoDatabase db;
  if (db.Open())
  {
    db.SetArtForItem(m_movieItem->GetVideoInfoTag()->m_iDbId, m_movieItem->GetVideoInfoTag()->m_type, "fanart", result);
    db.Close();
  }

  CUtil::DeleteVideoDatabaseDirectoryCache(); // to get them new thumbs to show
  m_movieItem->SetArt("fanart", result);
  m_hasUpdatedThumb = true;

  // Update our screen
  Update();
}

void CGUIDialogVideoInfo::PlayTrailer()
{
  CFileItem item;
  item.SetPath(m_movieItem->GetVideoInfoTag()->m_strTrailer);
  *item.GetVideoInfoTag() = *m_movieItem->GetVideoInfoTag();
  item.GetVideoInfoTag()->m_streamDetails.Reset();
  item.GetVideoInfoTag()->m_strTitle.Format("%s (%s)",m_movieItem->GetVideoInfoTag()->m_strTitle.c_str(),g_localizeStrings.Get(20410));
  CVideoThumbLoader::SetArt(item, m_movieItem->GetArt());
  item.GetVideoInfoTag()->m_iDbId = -1;
  item.GetVideoInfoTag()->m_iFileId = -1;

  // Close the dialog.
  Close(true);

  if (item.IsPlayList())
    CApplicationMessenger::Get().MediaPlay(item);
  else
    CApplicationMessenger::Get().PlayFile(item);
}

void CGUIDialogVideoInfo::SetLabel(int iControl, const CStdString &strLabel)
{
  if (strLabel.IsEmpty())
  {
    SET_CONTROL_LABEL(iControl, 416);  // "Not available"
  }
  else
  {
    SET_CONTROL_LABEL(iControl, strLabel);
  }
}

std::string CGUIDialogVideoInfo::GetThumbnail() const
{
  return m_movieItem->GetArt("thumb");
}

void CGUIDialogVideoInfo::AddItemPathToFileBrowserSources(VECSOURCES &sources, const CFileItem &item)
{
  if (!item.HasVideoInfoTag())
    return;

  CStdString itemDir = item.GetVideoInfoTag()->m_basePath;

  //season
  if (itemDir.IsEmpty())
    itemDir = item.GetVideoInfoTag()->GetPath();

  CFileItem itemTmp(itemDir, false);
  if (itemTmp.IsVideo())
    itemDir = URIUtils::GetParentPath(itemDir);

  if (!itemDir.IsEmpty() && CDirectory::Exists(itemDir))
  {
    CMediaSource itemSource;
    itemSource.strName = g_localizeStrings.Get(36041);
    itemSource.strPath = itemDir;
    sources.push_back(itemSource);
  }
}

int CGUIDialogVideoInfo::ManageVideoItem(const CFileItemPtr &item)
{
  if (item == NULL || !item->IsVideoDb() || !item->HasVideoInfoTag() || item->GetVideoInfoTag()->m_iDbId < 0)
    return -1;

  CVideoDatabase database;
  if (!database.Open())
    return -1;

  VIDEODB_CONTENT_TYPE type = (VIDEODB_CONTENT_TYPE)item->GetVideoContentType();
  int dbId = item->GetVideoInfoTag()->m_iDbId;

  CContextButtons buttons;
  buttons.Add(CONTEXT_BUTTON_EDIT, 16105);

  if (type == VIDEODB_CONTENT_MOVIES || type == VIDEODB_CONTENT_TVSHOWS)
    buttons.Add(CONTEXT_BUTTON_EDIT_SORTTITLE, 16107);

  if (item->m_bIsFolder)
  {
    // Have both options for folders since we don't know whether all childs are watched/unwatched
    buttons.Add(CONTEXT_BUTTON_MARK_UNWATCHED, 16104); //Mark as UnWatched
    buttons.Add(CONTEXT_BUTTON_MARK_WATCHED, 16103);   //Mark as Watched
  }
  else
  {
    if (item->GetOverlayImage().Equals("OverlayWatched.png"))
      buttons.Add(CONTEXT_BUTTON_MARK_UNWATCHED, 16104); //Mark as UnWatched
    else
      buttons.Add(CONTEXT_BUTTON_MARK_WATCHED, 16103);   //Mark as Watched
  }

  if (type == VIDEODB_CONTENT_MOVIES)
  {
    // only show link/unlink if there are tvshows available
    if (database.HasContent(VIDEODB_CONTENT_TVSHOWS))
    {
      buttons.Add(CONTEXT_BUTTON_LINK_MOVIE, 20384);
      if (database.IsLinkedToTvshow(dbId))
        buttons.Add(CONTEXT_BUTTON_UNLINK_MOVIE, 20385);
    }

    // set or change movie set the movie belongs to
    buttons.Add(CONTEXT_BUTTON_SET_MOVIESET, 20465);
  }

  if (type == VIDEODB_CONTENT_EPISODES &&
      item->GetVideoInfoTag()->m_iBookmarkId > 0)
    buttons.Add(CONTEXT_BUTTON_UNLINK_BOOKMARK, 20405);

  bool result = false;
  int button = CGUIDialogContextMenu::ShowAndGetChoice(buttons);
  if (button >= 0)
  {
    switch ((CONTEXT_BUTTON)button)
    {
      case CONTEXT_BUTTON_EDIT:
        result = UpdateVideoItemTitle(item);
        break;

      case CONTEXT_BUTTON_EDIT_SORTTITLE:
        result = UpdateVideoItemSortTitle(item);
        break;

      case CONTEXT_BUTTON_MARK_WATCHED:
        result = MarkWatched(item, true);
        break;

      case CONTEXT_BUTTON_MARK_UNWATCHED:
        result = MarkWatched(item, false);
        break;

      case CONTEXT_BUTTON_LINK_MOVIE:
        result = LinkMovieToTvShow(item, false, database);
        break;

      case CONTEXT_BUTTON_UNLINK_MOVIE:
        result = LinkMovieToTvShow(item, true, database);
        break;

      case CONTEXT_BUTTON_SET_MOVIESET:
      {
        CFileItemPtr selectedSet;
        if (GetSetForMovie(item.get(), selectedSet))
          result = SetMovieSet(item.get(), selectedSet.get());
        break;
      }

      case CONTEXT_BUTTON_UNLINK_BOOKMARK:
        database.DeleteBookMarkForEpisode(*item->GetVideoInfoTag());
        result = true;
        break;

      default:
        result = false;
        break;
    }
  }

  database.Close();

  if (result)
    return button;

  return -1;
}

//Add change a title's name
bool CGUIDialogVideoInfo::UpdateVideoItemTitle(const CFileItemPtr &pItem)
{
  // dont allow update while scanning
  if (g_application.IsVideoScanning())
  {
    CGUIDialogOK::ShowAndGetInput(257, 0, 14057, 0);
    return false;
  }

  CVideoDatabase database;
  if (!database.Open())
    return false;

  int iDbId = pItem->GetVideoInfoTag()->m_iDbId;
  CVideoInfoTag detail;
  VIDEODB_CONTENT_TYPE iType = (VIDEODB_CONTENT_TYPE)pItem->GetVideoContentType();
  if (iType == VIDEODB_CONTENT_MOVIES)
    database.GetMovieInfo("", detail, iDbId);
  else if (iType == VIDEODB_CONTENT_MOVIE_SETS)
    database.GetSetInfo(iDbId, detail);
  else if (iType == VIDEODB_CONTENT_EPISODES)
    database.GetEpisodeInfo(pItem->GetPath(), detail, iDbId);
  else if (iType == VIDEODB_CONTENT_TVSHOWS)
    database.GetTvShowInfo(pItem->GetVideoInfoTag()->m_strFileNameAndPath, detail, iDbId);
  else if (iType == VIDEODB_CONTENT_MUSICVIDEOS)
    database.GetMusicVideoInfo(pItem->GetVideoInfoTag()->m_strFileNameAndPath, detail, iDbId);

  // get the new title
  if (!CGUIKeyboardFactory::ShowAndGetInput(detail.m_strTitle, g_localizeStrings.Get(16105), false))
    return false;

  database.UpdateMovieTitle(iDbId, detail.m_strTitle, iType);
  return true;
}

bool CGUIDialogVideoInfo::MarkWatched(const CFileItemPtr &item, bool bMark)
{
  if (!CProfilesManager::Get().GetCurrentProfile().canWriteDatabases())
    return false;

  // dont allow update while scanning
  if (g_application.IsVideoScanning())
  {
    CGUIDialogOK::ShowAndGetInput(257, 0, 14057, 0);
    return false;
  }

  CVideoDatabase database;
  if (!database.Open())
    return false;

  CFileItemList items;
  if (item->m_bIsFolder)
  {
    CStdString strPath = item->GetPath();
    CDirectory::GetDirectory(strPath, items);
  }
  else
    items.Add(item);

  for (int i = 0; i < items.Size(); ++i)
  {
    CFileItemPtr pTmpItem = items[i];
    if (pTmpItem->m_bIsFolder)
    {
      MarkWatched(pTmpItem, bMark);
      continue;
    }

    if (pTmpItem->HasVideoInfoTag() &&
       ((bMark && pTmpItem->GetVideoInfoTag()->m_playCount) ||
        (!bMark && !pTmpItem->GetVideoInfoTag()->m_playCount)))
      continue;

#ifdef HAS_UPNP
    if (!URIUtils::IsUPnP(item->GetPath()) || !UPNP::CUPnP::MarkWatched(*pTmpItem, bMark))
#endif
    {
      // Clear resume bookmark
      if (bMark)
        database.ClearBookMarksOfFile(pTmpItem->GetPath(), CBookmark::RESUME);

      database.SetPlayCount(*pTmpItem, bMark ? 1 : 0);
    }
  }

  database.Close();

  return true;
}

bool CGUIDialogVideoInfo::GetMoviesForSet(const CFileItem *setItem, CFileItemList &originalMovies, CFileItemList &selectedMovies)
{
  CVideoDatabase videodb;
  if (!videodb.Open())
    return false;

  CStdString strHeading; strHeading.Format(g_localizeStrings.Get(20457));
  CStdString baseDir;
  baseDir.Format("videodb://movies/sets/%d", setItem->GetVideoInfoTag()->m_iDbId);

  if (!CDirectory::GetDirectory(baseDir, originalMovies) || originalMovies.Size() <= 0) // keep a copy of the original members of the set
    return false;

  CFileItemList listItems;
  if (!videodb.GetSortedVideos(MediaTypeMovie, "videodb://movies", SortDescription(), listItems) || listItems.Size() <= 0)
    return false;

  CGUIDialogSelect *dialog = (CGUIDialogSelect *)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
  if (dialog == NULL)
    return false;

  listItems.Sort(SortByLabel, SortOrderAscending, SortAttributeIgnoreArticle);

  dialog->Reset();
  dialog->SetMultiSelection(true);
  dialog->SetHeading(strHeading);
  dialog->SetItems(&listItems);
  vector<int> selectedIndices;
  for (int i = 0; i < originalMovies.Size(); i++)
  {
    for (int listIndex = 0; listIndex < listItems.Size(); listIndex++)
    {
      if (listItems.Get(listIndex)->GetVideoInfoTag()->m_iDbId == originalMovies[i]->GetVideoInfoTag()->m_iDbId)
      {
        selectedIndices.push_back(listIndex);
        break;
      }
    }
  }
  dialog->SetSelected(selectedIndices);
  dialog->EnableButton(true, 186);
  dialog->DoModal();

  if (dialog->IsConfirmed())
  {
    selectedMovies.Copy(dialog->GetSelectedItems());
    return (selectedMovies.Size() > 0);
  }
  else
    return false;
}

bool CGUIDialogVideoInfo::GetSetForMovie(const CFileItem *movieItem, CFileItemPtr &selectedSet)
{
  CVideoDatabase videodb;
  if (!videodb.Open())
    return false;

  CFileItemList listItems;
  CStdString baseDir = "videodb://movies/sets/";
  if (!CDirectory::GetDirectory(baseDir, listItems) || listItems.Size() <= 0)
    return false;
  listItems.Sort(SortByLabel, SortOrderAscending, SortAttributeIgnoreArticle);

  int currentSetId = 0;
  CStdString currentSetLabel;

  if (movieItem->GetVideoInfoTag()->m_iSetId > currentSetId)
  {
    currentSetId = movieItem->GetVideoInfoTag()->m_iSetId;
    currentSetLabel = videodb.GetSetById(currentSetId);
  }

  if (currentSetId > 0)
  {
    // add clear item
    CStdString strClear; strClear.Format(g_localizeStrings.Get(20467), currentSetLabel);
    CFileItemPtr clearItem(new CFileItem(strClear));
    clearItem->GetVideoInfoTag()->m_iDbId = -1; // -1 will be used to clear set
    listItems.AddFront(clearItem, 0);
    // add keep current set item
    CStdString strKeep; strKeep.Format(g_localizeStrings.Get(20469), currentSetLabel);
    CFileItemPtr keepItem(new CFileItem(strKeep));
    keepItem->GetVideoInfoTag()->m_iDbId = currentSetId;
    listItems.AddFront(keepItem, 1);
  }

  CGUIDialogSelect *dialog = (CGUIDialogSelect *)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
  if (dialog == NULL)
    return false;

  CStdString strHeading; 
  strHeading.Format(g_localizeStrings.Get(20466));
  dialog->Reset();
  dialog->SetHeading(strHeading);
  dialog->SetItems(&listItems);
  if (currentSetId >= 0)
  {
    for (int listIndex = 0; listIndex < listItems.Size(); listIndex++) 
    {
      if (listItems.Get(listIndex)->GetVideoInfoTag()->m_iDbId == currentSetId)
      {
        dialog->SetSelected(listIndex);
        break;
      }
    }
  }
  dialog->EnableButton(true, 20468); // new set via button
  dialog->DoModal();

  if (dialog->IsButtonPressed())
  { // creating new set
    CStdString newSetTitle;
    if (!CGUIKeyboardFactory::ShowAndGetInput(newSetTitle, g_localizeStrings.Get(20468), false))
      return false;
    int idSet = videodb.AddSet(newSetTitle);
    map<string, string> movieArt, setArt;
    if (!videodb.GetArtForItem(idSet, "set", setArt))
    {
      videodb.GetArtForItem(movieItem->GetVideoInfoTag()->m_iDbId, "movie", movieArt);
      videodb.SetArtForItem(idSet, "set", movieArt);
    }
    CFileItemPtr newSet(new CFileItem(newSetTitle));
    newSet->GetVideoInfoTag()->m_iDbId = idSet;
    selectedSet = newSet;
    return true;
  }
  else if (dialog->IsConfirmed())
  {
    selectedSet = dialog->GetSelectedItem();
    return (selectedSet != NULL);
  }
  else
    return false;
}

bool CGUIDialogVideoInfo::SetMovieSet(const CFileItem *movieItem, const CFileItem *selectedSet)
{
  CVideoDatabase videodb;
  if (!videodb.Open())
    return false;

  videodb.SetMovieSet(movieItem->GetVideoInfoTag()->m_iDbId, selectedSet->GetVideoInfoTag()->m_iDbId);
  return true;
}

bool CGUIDialogVideoInfo::UpdateVideoItemSortTitle(const CFileItemPtr &pItem)
{
  // dont allow update while scanning
  if (g_application.IsVideoScanning())
  {
    CGUIDialogOK::ShowAndGetInput(257, 0, 14057, 0);
    return false;
  }

  CVideoDatabase database;
  if (!database.Open())
    return false;

  int iDbId = pItem->GetVideoInfoTag()->m_iDbId;
  CVideoInfoTag detail;
  VIDEODB_CONTENT_TYPE iType = (VIDEODB_CONTENT_TYPE)pItem->GetVideoContentType();
  if (iType == VIDEODB_CONTENT_MOVIES)
    database.GetMovieInfo("", detail, iDbId);
  else if (iType == VIDEODB_CONTENT_TVSHOWS)
    database.GetTvShowInfo(pItem->GetVideoInfoTag()->m_strFileNameAndPath, detail, iDbId);

  // get the new sort title
  if (!CGUIKeyboardFactory::ShowAndGetInput(detail.m_strTitle, g_localizeStrings.Get(16107), false))
    return false;

  return database.UpdateVideoSortTitle(iDbId, detail.m_strTitle, iType);
}

bool CGUIDialogVideoInfo::LinkMovieToTvShow(const CFileItemPtr &item, bool bRemove, CVideoDatabase &database)
{
  int dbId = item->GetVideoInfoTag()->m_iDbId;

  CFileItemList list;
  if (bRemove)
  {
    vector<int> ids;
    if (!database.GetLinksToTvShow(dbId, ids))
      return false;

    for (unsigned int i = 0; i < ids.size(); ++i)
    {
      CVideoInfoTag tag;
      database.GetTvShowInfo("", tag, ids[i]);
      CFileItemPtr show(new CFileItem(tag));
      list.Add(show);
    }
  }
  else
  {
    database.GetTvShowsNav("videodb://tvshows/titles", list);

    // remove already linked shows
    vector<int> ids;
    if (!database.GetLinksToTvShow(dbId, ids))
      return false;

    for (int i = 0; i < list.Size(); )
    {
      size_t j;
      for (j = 0; j < ids.size(); ++j)
      {
        if (list[i]->GetVideoInfoTag()->m_iDbId == ids[j])
          break;
      }
      if (j == ids.size())
        i++;
      else
        list.Remove(i);
    }
  }

  int iSelectedLabel = 0;
  if (list.Size() > 1)
  {
    list.Sort(SortByLabel, SortOrderAscending, CSettings::Get().GetBool("filelists.ignorethewhensorting") ? SortAttributeIgnoreArticle : SortAttributeNone);
    CGUIDialogSelect* pDialog = (CGUIDialogSelect*)g_windowManager.GetWindow(WINDOW_DIALOG_SELECT);
    pDialog->Reset();
    pDialog->SetItems(&list);
    pDialog->SetHeading(20356);
    pDialog->DoModal();
    iSelectedLabel = pDialog->GetSelectedLabel();
  }

  if (iSelectedLabel > -1)
    return database.LinkMovieToTvshow(dbId, list[iSelectedLabel]->GetVideoInfoTag()->m_iDbId, bRemove);

  return false;
}
