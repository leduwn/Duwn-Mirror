; *** Inno Setup Vietnamese messages ***
; Tệp ngôn ngữ Tiếng Việt cho Duwn Mirror Setup

[LangOptions]
LanguageName=Tiếng Việt
LanguageID=$042A
LanguageCodePage=0

[Messages]

; *** Application titles
SetupAppTitle=Cài đặt Duwn Mirror
SetupWindowTitle=Cài đặt - %1
UninstallAppTitle=Gỡ cài đặt Duwn Mirror
UninstallAppFullTitle=Gỡ cài đặt %1

; *** Misc. common
InformationTitle=Thông tin
ConfirmTitle=Xác nhận
ErrorTitle=Lỗi

; *** Setup common elements
WizardInstalling=Đang cài đặt
WizardUninstalling=Đang gỡ cài đặt
ButtonBack=< &Quay lại
ButtonNext=&Tiếp tục >
ButtonInstall=&Cài đặt
ButtonOK=Đồng ý
ButtonCancel=Hủy
ButtonYes=&Có
ButtonYesToAll=Có cho &tất cả
ButtonNo=&Không
ButtonNoToAll=K&hông cho tất cả
ButtonFinish=&Hoàn tất
ButtonBrowse=&Duyệt…
ButtonWizardBrowse=D&uyệt…
ButtonNewFolder=Tạo &thư mục mới

; *** "Select Language" dialog per-language strings
SelectLanguageTitle=Chọn ngôn ngữ cài đặt
SelectLanguageLabel=Chọn ngôn ngữ sẽ sử dụng trong quá trình cài đặt:

; *** Common error messages
ErrorCreatingDir=Xảy ra lỗi khi tạo thư mục:%n%n%1%n%n%2

; *** Setup wizard pages
; --- Welcome page ---
WelcomeLabel1=Chào mừng bạn đến với trình cài đặt [name]
WelcomeLabel2=Trình cài đặt sẽ hướng dẫn bạn cài đặt [name] trên máy tính.%n%nDuwn Mirror là ứng dụng nhận phản chiếu và truyền phát màn hình iPhone/iPad không dây trên Windows, phục vụ trình chiếu, giảng dạy, demo, quay video và livestream.%n%nKhuyến nghị đóng tất cả các ứng dụng khác trước khi tiếp tục.

; --- "Password" page ---
WizardPassword=Mật khẩu
PasswordLabel1=Cài đặt này được bảo vệ bằng mật khẩu.
PasswordLabel3=Vui lòng nhập mật khẩu, sau đó nhấn Tiếp tục để tiếp tục. Mật khẩu có phân biệt chữ hoa, chữ thường.
PasswordEditLabel=&Mật khẩu:
IncorrectPassword=Mật khẩu đã nhập không chính xác. Vui lòng thử lại.

; --- "Select Destination Location" page ---
WizardSelectDir=Chọn thư mục cài đặt
SelectDirDesc=Nơi [name] sẽ được cài đặt?
SelectDirLabel3=Trình cài đặt sẽ cài đặt [name] vào thư mục sau.
SelectDirBrowseLabel=Để tiếp tục, nhấn Tiếp tục. Nếu bạn muốn chọn thư mục khác, nhấn Duyệt.
DiskSpaceGBLabel=Cần ít nhất %1 GB dung lượng đĩa trống.
DiskSpaceMBLabel=Cần ít nhất %1 MB dung lượng đĩa trống.
InvalidPath=Bạn phải nhập đường dẫn đầy đủ cùng với ký tự ổ đĩa; ví dụ:%n%nC:\APP%n%nhoặc đường dẫn mạng UNC theo dạng:%n%n\\server\share
InvalidDrive=Ổ đĩa bạn đã chọn không tồn tại hoặc không thể truy cập. Vui lòng chọn ổ đĩa khác.
DiskSpaceWarningTitle=Không đủ dung lượng đĩa
DiskSpaceWarning=Trình cài đặt cần ít nhất %1 KB dung lượng trống để cài đặt, nhưng ổ đĩa đã chọn chỉ còn %2 KB khả dụng.%n%nBạn có muốn tiếp tục không?
DirNameTooLong=Tên thư mục hoặc đường dẫn quá dài.
InvalidDirName=Tên thư mục không hợp lệ.
DirExistsTitle=Thư mục đã tồn tại
DirExists=Thư mục:%n%n%1%n%nđã tồn tại. Bạn vẫn muốn cài đặt vào thư mục này chứ?
DirDoesntExistTitle=Thư mục không tồn tại
DirDoesntExist=Thư mục:%n%n%1%n%nkhông tồn tại. Bạn có muốn tạo thư mục này không?

; --- "Select Components" page ---
WizardSelectComponents=Chọn thành phần cài đặt
SelectComponentsDesc=Những thành phần nào sẽ được cài đặt?
SelectComponentsLabel2=Chọn các thành phần bạn muốn cài đặt; bỏ chọn các thành phần bạn không muốn cài đặt. Sau đó nhấn Tiếp tục.
FullInstallation=Cài đặt đầy đủ
CompactInstallation=Cài đặt tối thiểu
CustomInstallation=Cài đặt tùy chỉnh
NoUninstallWarningTitle=Thành phần đã tồn tại
NoUninstallWarning=Trình cài đặt phát hiện các thành phần sau đã được cài đặt trên máy tính:%n%n%1%n%nBỏ chọn các thành phần này sẽ không gỡ bỏ chúng.%n%nBạn có muốn tiếp tục không?
ComponentSize1=%1 KB
ComponentSize2=%1 MB

; --- "Select Additional Tasks" page ---
WizardSelectTasks=Chọn tác vụ bổ sung
SelectTasksDesc=Các tác vụ bổ sung nào sẽ được thực hiện?
SelectTasksLabel2=Chọn các tác vụ bổ sung bạn muốn Trình cài đặt thực hiện trong khi cài đặt [name], sau đó nhấn Tiếp tục.

; --- "Select Start Menu Folder" page ---
WizardSelectProgramGroup=Chọn thư mục Start Menu
SelectStartMenuFolderDesc=Nơi Trình cài đặt đặt các lối tắt của chương trình?
SelectStartMenuFolderLabel3=Trình cài đặt sẽ tạo lối tắt của chương trình trong thư mục Start Menu sau.
SelectStartMenuFolderBrowseLabel=Để tiếp tục, nhấn Tiếp tục. Nếu bạn muốn chọn thư mục khác, nhấn Duyệt.

; --- "Ready to Install" page ---
WizardReady=Sẵn sàng cài đặt
ReadyLabel1=Trình cài đặt đã sẵn sàng bắt đầu cài đặt [name] trên máy tính của bạn.
ReadyLabel2a=Nhấn Cài đặt để tiếp tục cài đặt, hoặc nhấn Quay lại nếu bạn muốn xem lại hoặc thay đổi bất kỳ cài đặt nào.
ReadyLabel2b=Nhấn Cài đặt để tiếp tục cài đặt.
ReadyMemoUserInfo=Thông tin người dùng:
ReadyMemoDir=Thư mục cài đặt:
ReadyMemoType=Kiểu cài đặt:
ReadyMemoComponents=Thành phần đã chọn:
ReadyMemoGroup=Thư mục Start Menu:
ReadyMemoTasks=Tác vụ bổ sung:

; --- TDownloadWizardPage status and error messages ---
ButtonStopDownload=&Dừng tải
StopDownload=Bạn có chắc chắn muốn dừng quá trình tải xuống không?
ErrorDownloadAborted=Quá trình tải xuống đã bị hủy
ErrorDownloadFailed=Tải xuống thất bại: %1 %2
ErrorDownloadSizeFailed=Lấy kích thước tệp thất bại: %1 %2
ErrorProgress=Tiến độ không hợp lệ: %1 trên %2
ErrorFileSize=Kích thước tệp không hợp lệ: mong muốn %1, nhận được %2

; --- "Preparing to Install" page ---
WizardPreparing=Đang chuẩn bị cài đặt
PreparingDesc=Trình cài đặt đang chuẩn bị cài đặt [name] trên máy tính của bạn.
PreviousInstallNotCompleted=Quá trình cài đặt/gỡ cài đặt của một chương trình trước đó chưa hoàn tất. Bạn cần khởi động lại máy tính để hoàn thành quá trình đó.%n%nSau khi khởi động lại máy tính, hãy chạy lại Trình cài đặt để hoàn tất cài đặt [name].
CannotContinue=Trình cài đặt không thể tiếp tục. Vui lòng nhấn Hủy để thoát.
ApplicationsFound=Các ứng dụng sau đang sử dụng các tệp cần được cập nhật bởi Trình cài đặt. Bạn nên cho phép Trình cài đặt tự động đóng các ứng dụng này.
ApplicationsFound2=Các ứng dụng sau đang sử dụng các tệp cần được cập nhật bởi Trình cài đặt. Bạn nên cho phép Trình cài đặt tự động đóng các ứng dụng này. Sau khi cài đặt hoàn tất, Trình cài đặt sẽ cố gắng khởi động lại các ứng dụng.
CloseApplications=&Tự động đóng các ứng dụng
DontCloseApplications=&Không đóng các ứng dụng
ErrorCloseApplications=Trình cài đặt không thể tự động đóng tất cả các ứng dụng. Trước khi tiếp tục, bạn nên đóng tất cả các ứng dụng đang sử dụng các tệp cần được cập nhật.
PrepareToInstallNeedsRestart=Trình cài đặt cần khởi động lại máy tính. Vui lòng khởi động lại máy tính và chạy lại Trình cài đặt.

; --- "Installing" page ---
WizardInstalling=Đang cài đặt
InstallingLabel=Vui lòng đợi trong khi Trình cài đặt cài đặt [name] trên máy tính của bạn.

; --- "Setup Completed" page ---
FinishedHeadingLabel=Hoàn tất cài đặt [name]
FinishedLabelNoIcons=Trình cài đặt đã hoàn tất cài đặt [name] trên máy tính của bạn.
FinishedLabel=Trình cài đặt đã hoàn tất cài đặt [name] trên máy tính của bạn. Ứng dụng có thể được khởi chạy bằng cách chọn các biểu tượng đã cài đặt.
ClickFinish=Nhấn Hoàn tất để thoát khỏi Trình cài đặt.
ExitSetupTitle=Thoát Trình cài đặt
ExitSetupMessage=Quá trình cài đặt chưa hoàn tất. Nếu bạn thoát bây giờ, chương trình sẽ không được cài đặt.%n%nBạn có thể chạy lại Trình cài đặt vào lúc khác để hoàn tất cài đặt.%n%nThoát Trình cài đặt?
StatusClosingApplications=Đang đóng các ứng dụng…
StatusCreateDirs=Đang tạo các thư mục…
StatusExtractFiles=Đang giải nén các tệp…
StatusCreateIcons=Đang tạo các lối tắt…
StatusCreateIniEntries=Đang tạo các mục INI…
StatusCreateRegistryEntries=Đang tạo các mục registry…
StatusRegisterFiles=Đang đăng ký các tệp…
StatusRunProgram=Đang hoàn tất cài đặt…
StatusRestartingApplications=Đang khởi động lại các ứng dụng…
StatusUninstalling=Đang gỡ cài đặt…

; *** Uninstaller specific strings
UninstallStatusLabel=Vui lòng đợi trong khi [name] được gỡ bỏ khỏi máy tính của bạn.
UninstalledAll=%1 đã được gỡ bỏ thành công khỏi máy tính của bạn.
UninstalledMost=Quá trình gỡ cài đặt %1 đã hoàn tất.%n%nMột số thành phần không thể gỡ bỏ được. Bạn có thể xóa chúng thủ công.
UninstalledAndNeedsRestart=Để hoàn tất việc gỡ cài đặt %1, máy tính của bạn phải được khởi động lại.%n%nBạn có muốn khởi động lại ngay bây giờ không?
ConfirmUninstall=Bạn có chắc chắn muốn gỡ bỏ hoàn toàn %1 và tất cả các thành phần của ứng dụng không?
OnlyAdminCanUninstall=Chỉ người dùng có quyền quản trị mới có thể gỡ cài đặt chương trình này.
UninstallNotFound=Tệp "%1" không tồn tại. Không thể gỡ cài đặt.
UninstallOpenError=Tệp "%1" không thể mở được. Không thể gỡ cài đặt.
UninstallUnsupportedVer=Tệp nhật ký gỡ cài đặt "%1" có định dạng không được hỗ trợ bởi phiên bản này của trình gỡ cài đặt. Không thể gỡ cài đặt.
